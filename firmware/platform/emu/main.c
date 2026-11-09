/*
 * tredder-emu: the real firmware running on emulated board hardware.
 *
 *   tredder-emu [--port 8765] [--state DIR | --ephemeral] [--scale 1.5] [--headless]
 *               [--seed "public test words" --pin 1234 [--sweep-seed "..."]]
 *               [--auto-approve] [--debug-port N] [--pin-delay-ms N] [--idle-lock-ms N]
 *               [--sleep-ms N (0 = never)] [--battery PCT] [--battery-speed X] [--usb]
 *
 * DEVELOPMENT ONLY. The emulated flash and secure element live in plain files
 * on the PC (default %LOCALAPPDATA%\tredder-emu). Use public test phrases and
 * testnets only; a real seed belongs on real hardware.
 */
#include <windows.h>
#include <bcrypt.h>
#include <mmsystem.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/firmware.h"
#include "app/hal.h"
#include "app/se.h"
#include "app/vault.h"
#include "emu.h"
#include "wallet/mnemonic.h"
#include "wallet/secure.h"

emu_opts_t emu_opts = {8765, 0, 0, 1.0, -1, false, false, false, false, "", NULL, NULL, NULL};

static CRITICAL_SECTION cs;
static LARGE_INTEGER qpf, qp0;
static volatile LONG buttons[SRC_COUNT];

void emu_lock(void) { EnterCriticalSection(&cs); }
void emu_unlock(void) { LeaveCriticalSection(&cs); }

uint32_t emu_millis(void) {
  LARGE_INTEGER n;
  QueryPerformanceCounter(&n);
  return (uint32_t)((n.QuadPart - qp0.QuadPart) * 1000 / qpf.QuadPart);
}

void emu_random(uint8_t *out, size_t n) {
  if (BCryptGenRandom(NULL, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    emu_logf("FATAL: OS random generator failed");
    ExitProcess(3);
  }
}

/* ---------------------------------------------------------------- state files */
static void state_path(const char *name, char *out, size_t n) { snprintf(out, n, "%s\\%s", emu_opts.state_dir, name); }

bool emu_read_file(const char *name, void *buf, size_t n) {
  char p[600];
  state_path(name, p, sizeof(p));
  FILE *f = fopen(p, "rb");
  if (!f) return false;
  bool ok = fread(buf, 1, n, f) == n;
  fclose(f);
  return ok;
}

bool emu_write_file(const char *name, const void *buf, size_t n) {
  char p[600], tmp[610];
  state_path(name, p, sizeof(p));
  snprintf(tmp, sizeof(tmp), "%s.tmp", p);
  FILE *f = fopen(tmp, "wb");
  if (!f) return false;
  bool ok = fwrite(buf, 1, n, f) == n;
  ok = fflush(f) == 0 && ok;
  fclose(f);
  return ok && MoveFileExA(tmp, p, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

/* ---------------------------------------------------------------- log */
static char logring[LOG_LINES][160];
static int lognext, logcount;
static HANDLE out_handle = INVALID_HANDLE_VALUE;

void emu_logf(const char *fmt, ...) {
  char msg[300];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  SYSTEMTIME t;
  GetLocalTime(&t);
  char line[340];
  int n = snprintf(line, sizeof(line), "%02d:%02d:%02d  %s\r\n", t.wHour, t.wMinute, t.wSecond, msg);
  emu_lock();
  snprintf(logring[lognext], sizeof(logring[0]), "%02d:%02d:%02d  %s", t.wHour, t.wMinute, t.wSecond, msg);
  lognext = (lognext + 1) % LOG_LINES;
  if (logcount < LOG_LINES) logcount++;
  if (out_handle != INVALID_HANDLE_VALUE && out_handle != NULL) {
    DWORD w;
    WriteFile(out_handle, line, (DWORD)n, &w, NULL);
  }
  emu_unlock();
  emu_request_repaint();
}

void emu_log_lines(char out[LOG_LINES][160], int *n) {
  emu_lock();
  for (int i = 0; i < logcount; i++) memcpy(out[i], logring[(lognext - logcount + i + LOG_LINES) % LOG_LINES], 160);
  *n = logcount;
  emu_unlock();
}

size_t hal_log_recent(char *out, size_t max) {
  static char lines[LOG_LINES][160];
  int n = 0;
  emu_log_lines(lines, &n);
  if (!max) return 0;
  /* the newest lines that fit, oldest of them first */
  int first = n;
  size_t need = 1;
  while (first > 0 && need + strlen(lines[first - 1]) + 1 <= max) need += strlen(lines[--first]) + 1;
  size_t len = 0;
  for (int i = first; i < n; i++) {
    size_t l = strlen(lines[i]);
    memcpy(out + len, lines[i], l);
    len += l;
    out[len++] = 10;
  }
  out[len] = 0;
  return len;
}

void hal_log(const char *fmt, ...) {
  char msg[280];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  emu_logf("%s", msg);
}

/* trezor-crypto calls this if a fault-injection check trips. */
void tc_fault_handler(const char *msg) {
  emu_logf("FAULT DETECTED: %s (device would wipe RAM and reset)", msg);
  ExitProcess(2);
}

/* ---------------------------------------------------------------- HAL: time, buttons, LCD, TRNG */
uint32_t hal_millis(void) { return emu_millis(); }

void hal_idle(uint32_t max_ms) {
  /* WFE on the RP2350; here: wait for socket activity, max 5 ms (button sampling). */
  ws_poll_io((int)(max_ms < 5 ? max_ms : 5));
}

void emu_buttons_set(int src, uint8_t mask) { InterlockedExchange(&buttons[src], mask); }
uint8_t emu_buttons(void) {
  LONG v = 0;
  for (int i = 0; i < SRC_COUNT; i++) v |= buttons[i];
  return (uint8_t)v;
}
uint8_t hal_buttons_raw(void) { return emu_buttons(); }

void hal_lcd_reset(void) { lcd_model_reset(); }
void hal_lcd_cmd(uint8_t c) { lcd_model_cmd(c); }
void hal_lcd_data(const uint8_t *d, size_t n) { lcd_model_data(d, n); }
void hal_lcd_backlight(bool on) { lcd_model_backlight(on); }
void hal_lcd_power(bool on) { lcd_model_power(on); }

void hal_trng_raw(uint8_t *out, size_t n) { emu_random(out, n); }

/* ---------------------------------------------------------------- the MCU */
static uint16_t words_buf[24];

static bool parse_words(const char *phrase, uint16_t *idx, size_t *count) {
  size_t bad = 0;
  return wl_mnemonic_parse(phrase, idx, count, &bad) == WL_OK;
}

static void provision(void) {
  /* Test provisioning ("load device"), emulator only: uses the real vault code. */
  size_t n = 0, ns = 0;
  uint16_t sweep[24];
  if (!parse_words(emu_opts.seed, words_buf, &n)) {
    emu_logf("--seed is not a valid BIP39 phrase");
    ExitProcess(1);
  }
  const char *pin = emu_opts.pin ? emu_opts.pin : "1234";
  if (vault_create(words_buf, n, pin) != VAULT_OK) {
    emu_logf("provisioning failed");
    ExitProcess(1);
  }
  if (emu_opts.sweep_seed) {
    if (!parse_words(emu_opts.sweep_seed, sweep, &ns) || vault_set_sweep(sweep, ns) != VAULT_OK) {
      emu_logf("--sweep-seed is not a valid BIP39 phrase");
      ExitProcess(1);
    }
  }
  vault_lock();
  wl_wipe(words_buf, sizeof(words_buf));
  wl_wipe(sweep, sizeof(sweep));
  emu_logf("provisioned test wallet (%u words), PIN set", (unsigned)n);
}

/* ---------------------------------------------------------------- sleep */
static volatile LONG sleeping;
static volatile LONG power; /* 0 on, 1 switching off, 2 off (MCU halted, sockets closed) */

bool emu_sleeping(void) { return sleeping != 0; }

static _Noreturn void halt_for_power_off(void) {
  ws_stop();
  dbg_stop();
  InterlockedExchange(&power, 2);
  for (;;) Sleep(1000);
}

/* RP2350 DORMANT until a button edge. The radio is off: the phone link closes
   and nothing can connect until wake-up. (The debug link and robot finger are
   test equipment outside the chip, so they keep running and can press a button.) */
void hal_sleep(void) {
  InterlockedExchange(&sleeping, 1);
  ws_stop();
  emu_logf("asleep: display unpowered, radio off; press any button to wake");
  emu_request_repaint();
  while (!emu_buttons()) {
    if (power) halt_for_power_off();
    dbg_poll();
    robot_step(emu_millis());
    Sleep(5);
  }
  if (!ws_start(emu_opts.port)) emu_logf("phone link port %d could not be reopened", emu_opts.port);
  InterlockedExchange(&sleeping, 0);
  emu_request_repaint();
}

/* ---------------------------------------------------------------- SW1 power switch */
static int g_argc;
static char **g_argv;
bool emu_powered(void) { return power == 0; }

static void power_on_again(void) {
  /* New process = RAM gone, the chips' files stay. Never re-provision a test seed. */
  char cmd[4096];
  int n = snprintf(cmd, sizeof(cmd), "\"%s\"", g_argv[0]);
  for (int i = 1; i < g_argc && n < (int)sizeof(cmd) - 8; i++) {
    const char *a = g_argv[i];
    if ((!strcmp(a, "--seed") || !strcmp(a, "--sweep-seed") || !strcmp(a, "--pin")) && i + 1 < g_argc) {
      i++;
      continue;
    }
    n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, " \"%s\"", a);
  }
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
    emu_logf("power on failed (error %lu)", GetLastError());
    return;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  ExitProcess(0);
}

void emu_power_switch(void) {
  if (power == 0) {
    if (emu_opts.ephemeral) {
      emu_logf("SW1: a power cycle needs persistent flash (run without --ephemeral)");
      return;
    }
    emu_logf("SW1 OFF: MCU stopped, RAM lost; flash + secure element keep their contents");
    InterlockedExchange(&power, 1);
  } else if (power == 2) {
    power_on_again();
  }
}

static DWORD WINAPI mcu_thread(LPVOID arg) {
  (void)arg;
  se_init();
  if (emu_opts.seed) {
    vault_init();
    provision();
  }
  fw_init();
  for (;;) {
    if (power) halt_for_power_off();
    fw_poll();
    uint32_t now = hal_millis();
    robot_step(now);
    dbg_poll();
    hal_idle(5);
  }
  return 0;
}

/* ---------------------------------------------------------------- startup */
static void usage(void) {
  emu_logf("usage: tredder-emu [--port N] [--state DIR | --ephemeral] [--scale 0.75-3] [--headless]");
  emu_logf("                   [--seed \"words\" --pin 1234 [--sweep-seed \"words\"]] [--auto-approve] [--debug-port N]");
}

static void attach_output(void) {
  out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out_handle == NULL || out_handle == INVALID_HANDLE_VALUE) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  }
}

int main(int argc, char **argv) {
  InitializeCriticalSection(&cs);
  QueryPerformanceFrequency(&qpf);
  QueryPerformanceCounter(&qp0);
  timeBeginPeriod(1);
  attach_output();

  const char *state = NULL;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    const char *v = i + 1 < argc ? argv[i + 1] : NULL;
    if (!strcmp(a, "--port") && v) emu_opts.port = atoi(v), i++;
    else if (!strcmp(a, "--debug-port") && v) emu_opts.debug_port = atoi(v), i++;
    else if (!strcmp(a, "--scale") && v) emu_opts.scale = atof(v), i++;
    else if (!strcmp(a, "--state") && v) state = v, i++;
    else if (!strcmp(a, "--seed") && v) emu_opts.seed = v, i++;
    else if (!strcmp(a, "--sweep-seed") && v) emu_opts.sweep_seed = v, i++;
    else if (!strcmp(a, "--pin") && v) emu_opts.pin = v, i++;
    else if (!strcmp(a, "--pin-delay-ms") && v) fw_config.pin_delay_ms = (uint32_t)atoi(v), i++;
    else if (!strcmp(a, "--idle-lock-ms") && v) fw_config.idle_lock_ms = (uint32_t)atoi(v), i++;
    else if (!strcmp(a, "--sleep-ms") && v) fw_config.sleep_ms = (uint32_t)atoi(v), i++;
    else if (!strcmp(a, "--battery") && v) emu_opts.battery_pct = atoi(v), i++;
    else if (!strcmp(a, "--battery-speed") && v) emu_opts.battery_speed = atof(v), i++;
    else if (!strcmp(a, "--usb")) emu_opts.usb = true;
    else if (!strcmp(a, "--ephemeral")) emu_opts.ephemeral = true;
    else if (!strcmp(a, "--headless")) emu_opts.headless = true;
    else if (!strcmp(a, "--auto-approve")) emu_opts.auto_approve = true;
    else {
      usage();
      return 1;
    }
  }
  if (emu_opts.scale != 0 && (emu_opts.scale < 0.75 || emu_opts.scale > 3)) emu_opts.scale = 0;
  g_argc = argc, g_argv = argv;
  if (state) {
    snprintf(emu_opts.state_dir, sizeof(emu_opts.state_dir), "%s", state);
  } else {
    const char *la = getenv("LOCALAPPDATA");
    snprintf(emu_opts.state_dir, sizeof(emu_opts.state_dir), "%s\\tredder-emu", la ? la : ".");
  }
  if (!emu_opts.ephemeral) CreateDirectoryA(emu_opts.state_dir, NULL);
  if (emu_opts.seed && !emu_opts.ephemeral) emu_logf("WARNING: --seed overwrites the emulator's stored wallet");

  battery_model_init(emu_opts.battery_pct);
  if (emu_opts.usb) battery_model_usb(true);
  flash_model_load();
  se_model_load();
  sd_model_load();
  if (!ws_start(emu_opts.port)) {
    char m[160];
    snprintf(m, sizeof(m), "Port %d is already in use.\nIs another tredder emulator or simulator running?", emu_opts.port);
    emu_logf("%s", m);
    if (!emu_opts.headless) MessageBoxA(NULL, m, "tredder emulator", MB_ICONERROR);
    return 1;
  }
  if (emu_opts.debug_port && !dbg_start(emu_opts.debug_port)) {
    emu_logf("debug port %d unavailable", emu_opts.debug_port);
    return 1;
  }
  emu_logf("tredder emulator: phone link ws://127.0.0.1:%d, state %s", emu_opts.port,
           emu_opts.ephemeral ? "(in memory)" : emu_opts.state_dir);
  emu_logf("DEVELOPMENT ONLY: use public test phrases and testnets");

  if (emu_opts.headless) {
    mcu_thread(NULL);
    return 0;
  }
  CreateThread(NULL, 0, mcu_thread, NULL, 0, NULL);
  win_run();
  return 0;
}
