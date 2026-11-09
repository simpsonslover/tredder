/*
 * RP2350 HAL, phone link: BLE GATT peripheral on the Pico 2 W's CYW43 radio (BTstack, poll
 * mode: every BTstack callback runs inside cyw43_arch_poll() on the main loop, so nothing here
 * races with the firmware). protocol/PROTOCOL.md sections 1-2.
 *
 *   RX 7a3f0002 (write with response): one frame per write -> rx queue -> hal_link_recv()
 *   TX 7a3f0003 (notify): hal_link_send() -> tx queue -> one notification per CAN_SEND_NOW
 *
 * The link itself is not encrypted or bonded at the BLE level: the firmware's Noise channel
 * encrypts and authenticates every message end to end (WL_PROTO_NOISE). Trade-off: someone
 * nearby can see THAT a phone talks to the device and the message sizes, never the content.
 */
#include <string.h>

#include "app/hal.h"
#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "tredder.h" /* generated from tredder.gatt */

#define RX_HANDLE ATT_CHARACTERISTIC_7a3f0002_5c1e_4c6b_9f2e_6b7d2a1e0b10_01_VALUE_HANDLE
#define TX_HANDLE ATT_CHARACTERISTIC_7a3f0003_5c1e_4c6b_9f2e_6b7d2a1e0b10_01_VALUE_HANDLE
#define TX_CCCD ATT_CHARACTERISTIC_7a3f0003_5c1e_4c6b_9f2e_6b7d2a1e0b10_01_CLIENT_CONFIGURATION_HANDLE

#define SLOT 256u     /* max ATT payload (MTU 251 - 3, rounded up) */
#define RX_SLOTS 16u
#define TX_SLOTS 32u
#define SEND_TIMEOUT_MS 5000u

typedef struct {
  uint16_t n;
  uint8_t d[SLOT];
} slot_t;

static struct {
  bool ready; /* cyw43_arch_init done: only then may anything poll the radio */
  bool up;
  hci_con_handle_t con;
  bool notify;
  uint16_t mtu;
  uint32_t session;
  slot_t rx[RX_SLOTS], tx[TX_SLOTS];
  unsigned rx_head, rx_tail, tx_head, tx_tail;
  btstack_packet_callback_registration_t hci_cb;
} B;

/* Advertising: flags + our 128-bit service UUID (little-endian); the name in the scan response. */
static const uint8_t ADV[] = {
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    0x11, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
    0x10, 0x0b, 0x1e, 0x2a, 0x7d, 0x6b, 0x2e, 0x9f, 0x6b, 0x4c, 0x1e, 0x5c, 0x01, 0x00, 0x3f, 0x7a};
static const uint8_t SCAN_RSP[] = {0x08, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, 't', 'r', 'e', 'd', 'd', 'e', 'r'};

static void link_reset(void) {
  B.notify = false;
  B.mtu = ATT_DEFAULT_MTU;
  B.rx_head = B.rx_tail = B.tx_head = B.tx_tail = 0;
  memset(B.rx, 0, sizeof(B.rx)); /* frames are ciphertext, but leave nothing behind */
  memset(B.tx, 0, sizeof(B.tx));
}

static void kick_tx(void) {
  if (B.up && B.notify && B.tx_head != B.tx_tail) att_server_request_can_send_now_event(B.con);
}

static void packet_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
  (void)channel, (void)size;
  if (type != HCI_EVENT_PACKET) return;
  switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
      if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) hal_log("BLE ready, advertising as tredder");
      break;
    case ATT_EVENT_CONNECTED:
      link_reset();
      B.con = att_event_connected_get_handle(packet);
      B.up = true;
      B.session++;
      break;
    case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
      B.mtu = att_event_mtu_exchange_complete_get_MTU(packet);
      break;
    case ATT_EVENT_DISCONNECTED:
      B.up = false;
      B.con = HCI_CON_HANDLE_INVALID;
      link_reset();
      B.session++;
      break;
    case ATT_EVENT_CAN_SEND_NOW:
      if (B.up && B.tx_head != B.tx_tail) {
        slot_t *s = &B.tx[B.tx_tail % TX_SLOTS];
        att_server_notify(B.con, TX_HANDLE, s->d, s->n);
        B.tx_tail++;
        kick_tx();
      }
      break;
    default:
      break;
  }
}

static int att_write(hci_con_handle_t con, uint16_t handle, uint16_t mode, uint16_t offset, uint8_t *buf, uint16_t n) {
  (void)offset;
  if (con != B.con) return 0;
  if (handle == TX_CCCD) {
    B.notify = n >= 2 && little_endian_read_16(buf, 0) == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;
    kick_tx(); /* frames queued before the app subscribed (e.g. the handshake reply) */
    return 0;
  }
  if (handle != RX_HANDLE) return 0;
  if (mode != ATT_TRANSACTION_MODE_NONE) return ATT_ERROR_REQUEST_NOT_SUPPORTED; /* no long/prepared writes */
  if (n > SLOT || B.rx_head - B.rx_tail >= RX_SLOTS) return ATT_ERROR_INSUFFICIENT_RESOURCES;
  slot_t *s = &B.rx[B.rx_head % RX_SLOTS];
  memcpy(s->d, buf, n);
  s->n = n;
  B.rx_head++;
  return 0;
}

/* Called once from main() before fw_init(). */
bool hal_ble_init(void) {
  if (cyw43_arch_init()) {
    hal_log("CYW43 radio init failed");
    return false;
  }
  B.ready = true;
  B.con = HCI_CON_HANDLE_INVALID;
  link_reset();
  l2cap_init();
  sm_init();
  sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
  sm_set_authentication_requirements(0); /* no BLE bonding: Noise authenticates (see top) */
  att_server_init(profile_data, NULL, att_write);
  B.hci_cb.callback = packet_handler;
  hci_add_event_handler(&B.hci_cb);
  att_server_register_packet_handler(packet_handler);
  bd_addr_t null_addr = {0};
  gap_advertisements_set_params(160, 320, 0, 0, null_addr, 0x07, 0x00); /* 100-200 ms */
  gap_advertisements_set_data(sizeof(ADV), (uint8_t *)ADV);
  gap_scan_response_set_data(sizeof(SCAN_RSP), (uint8_t *)SCAN_RSP);
  gap_advertisements_enable(1);
  hci_power_control(HCI_POWER_ON);
  return true;
}

/* USB power present? On the Pico 2 W VBUS is read through the radio chip (WL_GPIO 2), so only
   after cyw43_arch_init: -1 = unknown yet. Works on a wall charger too (tud_mounted does not). */
int hal_vbus(void) {
  if (!B.ready) return -1;
  return cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN) ? 1 : 0;
}

/* ---------------------------------------------------------------- hal.h link API */
bool hal_link_connected(void) { return B.up; }
uint32_t hal_link_session(void) { return B.session; }

size_t hal_link_frame_size(void) {
  size_t n = B.mtu > 3 ? B.mtu - 3u : 20u;
  return n > SLOT ? SLOT : n;
}

size_t hal_link_recv(uint8_t *buf, size_t max) {
  if (B.rx_head == B.rx_tail) return 0;
  slot_t *s = &B.rx[B.rx_tail % RX_SLOTS];
  size_t n = s->n <= max ? s->n : 0; /* oversize: dropped (the framing layer then resets) */
  memcpy(buf, s->d, n);
  memset(s, 0, sizeof(*s));
  B.rx_tail++;
  return n;
}

bool hal_link_send(const uint8_t *frame, size_t n) {
  if (!B.up || n > hal_link_frame_size()) return false;
  uint32_t t0 = to_ms_since_boot(get_absolute_time());
  while (B.ready && B.tx_head - B.tx_tail >= TX_SLOTS) { /* queue full: let the radio drain it */
    cyw43_arch_poll();
    if (!B.up || to_ms_since_boot(get_absolute_time()) - t0 > SEND_TIMEOUT_MS) return false;
  }
  slot_t *s = &B.tx[B.tx_head % TX_SLOTS];
  memcpy(s->d, frame, n);
  s->n = (uint16_t)n;
  B.tx_head++;
  kick_tx();
  return true;
}

/* ---------------------------------------------------------------- idle / sleep (radio stays serviced) */
void hal_idle(uint32_t max_ms) {
  if (!B.ready) { /* radio not started yet (the display comes up first): plain sleep */
    sleep_ms(max_ms < 1 ? 1 : (max_ms > 5 ? 5 : max_ms));
    return;
  }
  cyw43_arch_poll();
  if (B.rx_head != B.rx_tail) return; /* work waiting */
  cyw43_arch_wait_for_work_until(make_timeout_time_ms(max_ms < 1 ? 1 : max_ms));
  cyw43_arch_poll();
}

/* Demo sleep: display off, radio keeps advertising; wakes on a button or when a phone connects. */
void hal_sleep(void) {
  uint32_t s = B.session;
  while (hal_buttons_raw() && B.session == s) hal_idle(10);
  while (!hal_buttons_raw() && B.session == s) hal_idle(20);
}
