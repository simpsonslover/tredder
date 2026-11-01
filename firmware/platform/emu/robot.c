/*
 * The "robot finger": presses the emulated buttons, nothing else. It only
 * sees what the screen shows (ui_debug, emulator builds only), exactly like a
 * person would, so every firmware check and screen still runs.
 *
 *  - typing helper: a key typed on the PC keyboard (digit on the PIN pad,
 *    letter in the word picker) is turned into the Up/Down/OK presses that
 *    select it. The real device has only the four buttons.
 *  - --auto-approve (automated tests only): unlocks with --pin and approves
 *    every request prompt by paging to the end and pressing OK.
 */
#include <string.h>

#include "app/hal.h"
#include "app/proto.h"
#include "app/ui.h"
#include "app/vault.h"
#include "emu.h"

#define HOLD_MS 22
#define GAP_MS 22

static struct {
  char queue[64];
  int qlen;
  uint8_t pressing;
  uint32_t until;
  const char *auto_pin;
  int auto_pin_pos;
} R;

void robot_press(uint8_t btn) {
  emu_lock();
  if (!R.pressing && !R.until) {
    R.pressing = btn;
    R.until = 0;
  }
  emu_unlock();
}

void robot_type(char c) {
  emu_lock();
  if (R.qlen < (int)sizeof(R.queue)) R.queue[R.qlen++] = c;
  emu_unlock();
}

static int find_item(const ui_debug_t *d, const char *s) {
  for (int i = 0; i < d->nitems; i++)
    if (!strcmp(d->items[i], s)) return i;
  return -1;
}

/* Next button to move the cursor to `target` (shortest way), or OK if there. */
static uint8_t move_to(const ui_debug_t *d, int target) {
  if (target == d->cursor) return HAL_BTN_CONFIRM;
  int n = d->nitems, down = (target - d->cursor + n) % n, up = (d->cursor - target + n) % n;
  return down <= up ? HAL_BTN_DOWN : HAL_BTN_UP;
}

/* What would a finger press next to enter character c? 0 = nothing sensible (drop c). */
static uint8_t key_for(const ui_debug_t *d, char c) {
  if (!strcmp(d->kind, "pin")) {
    char s[2] = {c, 0};
    int t = c == '\n' ? find_item(d, "✓") : c == '\b' ? find_item(d, "←") : find_item(d, s);
    return t >= 0 ? move_to(d, t) : 0;
  }
  if (!strcmp(d->kind, "word")) {
    if (c == '\b') return HAL_BTN_BACK;
    if (c == '\n') return d->nitems && !strncmp(d->items[0], "✓", 3) ? move_to(d, 0) : 0;
    char s[2] = {c, 0};
    int t = find_item(d, s);
    return t >= 0 ? move_to(d, t) : 0;
  }
  if (c == '\n') return HAL_BTN_CONFIRM;
  if (c == '\b') return HAL_BTN_BACK;
  return 0;
}

static uint8_t auto_step(const ui_debug_t *d) {
  if (!emu_opts.auto_approve) return 0;
  /* Locked home screen: unlock with the configured PIN. */
  if (!strcmp(d->kind, "menu") && vault_initialized() && !vault_unlocked() && d->nitems == 1 &&
      !strcmp(d->items[0], "Unlock") && emu_opts.pin) {
    R.auto_pin = emu_opts.pin;
    R.auto_pin_pos = 0;
    return HAL_BTN_CONFIRM;
  }
  if (!strcmp(d->kind, "pin") && emu_opts.pin) {
    if (!R.auto_pin) R.auto_pin = emu_opts.pin, R.auto_pin_pos = 0;
    char c = R.auto_pin[R.auto_pin_pos];
    uint8_t b = key_for(d, c ? c : '\n');
    if (b == HAL_BTN_CONFIRM) {
      if (c) R.auto_pin_pos++;
      else R.auto_pin = NULL;
    }
    return b;
  }
  /* Request prompts: page through everything, then approve. */
  if (!strcmp(d->kind, "pages") && proto_waiting_user()) return d->page + 1 < d->npages ? HAL_BTN_DOWN : HAL_BTN_CONFIRM;
  return 0;
}

void robot_step(uint32_t now) {
  emu_lock();
  if (R.pressing) { /* finish the current press */
    if (!R.until) {
      emu_buttons_set(SRC_ROBOT, R.pressing);
      R.until = now + HOLD_MS;
    } else if ((int32_t)(now - R.until) >= 0) {
      emu_buttons_set(SRC_ROBOT, 0);
      R.pressing = 0;
      R.until = now + GAP_MS;
    }
    emu_unlock();
    return;
  }
  if (R.until && (int32_t)(now - R.until) < 0) {
    emu_unlock();
    return;
  }
  R.until = 0;
  ui_debug_t d;
  ui_debug(&d);
  uint8_t b = 0;
  if (R.qlen) {
    char c = R.queue[0];
    bool raw = c >= 1 && c <= 4; /* debug link: a direct button press */
    b = raw ? (uint8_t)(c == 1 ? HAL_BTN_UP : c == 2 ? HAL_BTN_DOWN : c == 3 ? HAL_BTN_BACK : HAL_BTN_CONFIRM) : key_for(&d, c);
    if (raw || b == 0 || b == HAL_BTN_CONFIRM || b == HAL_BTN_BACK || (strcmp(d.kind, "pin") && strcmp(d.kind, "word"))) {
      memmove(R.queue, R.queue + 1, (size_t)--R.qlen); /* consumed (or not applicable) */
    }
  } else {
    b = auto_step(&d);
  }
  if (b) R.pressing = b;
  emu_unlock();
}

bool robot_idle(void) {
  emu_lock();
  bool idle = !R.qlen && !R.pressing && !R.until;
  emu_unlock();
  return idle;
}
