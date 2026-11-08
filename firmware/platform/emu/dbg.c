/*
 * Debug link for automated tests (emulator only, 127.0.0.1, off unless
 * --debug-port is given). Line commands, one reply line each:
 *   screen            -> JSON of what is on the display + device status
 *   press up|down|back|ok
 *   type <text>       -> letters/digits via the robot finger ("\n" = select)
 *   enter             -> the select action of the current screen
 *   shot <file.bmp>   -> save what the display shows (24-bit BMP)
 * press/type/enter reply "ok" once the robot finger has finished.
 */
#include <winsock2.h>
#include <stdio.h>
#include <string.h>

#include "app/ui.h"
#include "app/vault.h"
#include "emu.h"

static SOCKET ls = INVALID_SOCKET, cs = INVALID_SOCKET;
static char line[512];
static size_t ll;
static bool reply_when_idle;

bool dbg_start(int port) {
  ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons((u_short)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (ls == INVALID_SOCKET || bind(ls, (struct sockaddr *)&a, sizeof(a)) || listen(ls, 1)) return false;
  u_long one = 1;
  ioctlsocket(ls, FIONBIO, &one);
  return true;
}

void dbg_stop(void) {
  if (cs != INVALID_SOCKET) closesocket(cs);
  if (ls != INVALID_SOCKET) closesocket(ls);
  cs = ls = INVALID_SOCKET;
}

static void put(const char *s) {
  if (cs != INVALID_SOCKET) send(cs, s, (int)strlen(s), 0);
}

static void json_str(char **o, const char *end, const char *s) {
  char *p = *o;
  if (p < end) *p++ = '"';
  for (; s && *s && p < end - 7; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') *p++ = '\\', *p++ = (char)c;
    else if (c < 0x20) p += sprintf(p, "\\u%04x", c);
    else *p++ = (char)c;
  }
  if (p < end) *p++ = '"';
  *o = p;
}

static void screen_json(void) {
  static char buf[8192];
  char *o = buf, *end = buf + sizeof(buf) - 64;
  ui_debug_t d;
  ui_debug(&d);
  o += sprintf(o, "{\"kind\":");
  json_str(&o, end, d.kind);
  o += sprintf(o, ",\"title\":");
  json_str(&o, end, d.title);
  o += sprintf(o, ",\"lines\":[");
  for (int i = 0; i < d.nlines; i++) {
    if (i) *o++ = ',';
    json_str(&o, end, d.lines[i]);
  }
  o += sprintf(o, "],\"items\":[");
  for (int i = 0; i < d.nitems; i++) {
    if (i) *o++ = ',';
    json_str(&o, end, d.items[i]);
  }
  battery_state_t bat;
  battery_model_read(&bat);
  o += sprintf(o,
               "],\"cursor\":%u,\"page\":%u,\"npages\":%u,\"initialized\":%s,\"locked\":%s,\"triesLeft\":%u,"
               "\"sleeping\":%s,\"batteryMv\":%u,\"charging\":%s}\n",
               d.cursor, d.page, d.npages, vault_initialized() ? "true" : "false", vault_unlocked() ? "false" : "true",
               vault_tries_left(), emu_sleeping() ? "true" : "false", bat.vbat_mv, bat.charging ? "true" : "false");
  put(buf);
}

/* "shot <path.bmp>": what the glass shows right now, as a 24-bit BMP (design review). */
static void shot(const char *path) {
  static uint32_t px[GLASS_W * GLASS_H];
  lcd_model_snapshot(px);
  FILE *f = fopen(path, "wb");
  if (!f) {
    put("error cannot write file\n");
    return;
  }
  const unsigned row = GLASS_W * 3, size = 54 + row * GLASS_H;
  uint8_t h[54] = {'B', 'M'};
  h[2] = (uint8_t)size, h[3] = (uint8_t)(size >> 8), h[4] = (uint8_t)(size >> 16);
  h[10] = 54, h[14] = 40;
  h[18] = (uint8_t)GLASS_W, h[19] = (uint8_t)(GLASS_W >> 8);
  h[22] = (uint8_t)GLASS_H, h[23] = (uint8_t)(GLASS_H >> 8);
  h[26] = 1, h[28] = 24;
  fwrite(h, 1, sizeof(h), f);
  static uint8_t line_px[GLASS_W * 3];
  for (int y = GLASS_H - 1; y >= 0; y--) { /* BMP rows go bottom-up */
    for (unsigned x = 0; x < GLASS_W; x++) {
      uint32_t c = px[y * GLASS_W + x];
      line_px[3 * x] = (uint8_t)c, line_px[3 * x + 1] = (uint8_t)(c >> 8), line_px[3 * x + 2] = (uint8_t)(c >> 16);
    }
    fwrite(line_px, 1, row, f);
  }
  fclose(f);
  put("ok\n");
}

static void command(char *c) {
  if (!strncmp(c, "shot ", 5)) {
    shot(c + 5);
    return;
  }
  if (!strcmp(c, "screen")) {
    screen_json();
    return;
  }
  if (!strncmp(c, "press ", 6)) {
    const char *b = c + 6;
    char code = !strcmp(b, "up") ? 1 : !strcmp(b, "down") ? 2 : !strcmp(b, "back") ? 3 : !strcmp(b, "ok") ? 4 : 0;
    if (!code) {
      put("error unknown button\n");
      return;
    }
    robot_type(code);
    reply_when_idle = true;
    return;
  }
  if (!strncmp(c, "type ", 5)) {
    for (const char *p = c + 5; *p; p++) robot_type(*p);
    reply_when_idle = true;
    return;
  }
  if (!strcmp(c, "enter")) {
    robot_type('\n');
    reply_when_idle = true;
    return;
  }
  put("error unknown command\n");
}

void dbg_poll(void) {
  if (ls == INVALID_SOCKET) return;
  if (cs == INVALID_SOCKET) {
    cs = accept(ls, NULL, NULL);
    if (cs != INVALID_SOCKET) {
      u_long one = 1;
      ioctlsocket(cs, FIONBIO, &one);
      ll = 0;
    }
    return;
  }
  if (reply_when_idle) {
    if (!robot_idle()) return;
    reply_when_idle = false;
    put("ok\n");
  }
  char b[256];
  int k = recv(cs, b, sizeof(b), 0);
  if (k == 0 || (k == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
    closesocket(cs);
    cs = INVALID_SOCKET;
    return;
  }
  for (int i = 0; i < k; i++) {
    if (b[i] == '\n') {
      line[ll] = 0;
      if (ll && line[ll - 1] == '\r') line[ll - 1] = 0;
      ll = 0;
      command(line);
      if (reply_when_idle) { /* finish this command before reading the next */
        /* keep the rest of the buffer for later: push it back as pending input */
        int rest = k - i - 1;
        if (rest > 0 && (size_t)rest < sizeof(line)) memcpy(line, b + i + 1, (size_t)rest), ll = (size_t)rest;
        return;
      }
    } else if (ll < sizeof(line) - 1) {
      line[ll++] = b[i];
    }
  }
}
