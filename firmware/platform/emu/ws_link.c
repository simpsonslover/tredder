/*
 * The phone link. On the board this is BLE GATT (one write / notification =
 * one protocol frame). Here it is a WebSocket server on 127.0.0.1 carrying
 * exactly the same frames as binary messages, so the app's SimTransport
 * (and the Android emulator through `adb reverse`) talks to the firmware.
 * One phone at a time: a new connection replaces the old one.
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>

#include "app/hal.h"
#include "emu.h"
#include "sha2.h"

#define FRAME_SIZE 244 /* BLE ATT MTU 247 - 3 */
#define INBUF 8192

static SOCKET lsock = INVALID_SOCKET, csock = INVALID_SOCKET;
static bool handshaken;
static uint8_t in[INBUF];
static size_t in_len;
static volatile LONG session;
static char peer[64];

static void set_nonblocking(SOCKET s) {
  u_long one = 1;
  ioctlsocket(s, FIONBIO, &one);
}

bool ws_start(int port) {
  WSADATA w;
  if (WSAStartup(MAKEWORD(2, 2), &w)) return false;
  lsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (lsock == INVALID_SOCKET) return false;
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons((u_short)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  BOOL excl = TRUE;
  setsockopt(lsock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&excl, sizeof(excl));
  if (bind(lsock, (struct sockaddr *)&a, sizeof(a)) || listen(lsock, 4)) {
    closesocket(lsock);
    lsock = INVALID_SOCKET;
    return false;
  }
  set_nonblocking(lsock);
  return true;
}

static void drop_client(const char *why) {
  if (csock == INVALID_SOCKET) return;
  closesocket(csock);
  csock = INVALID_SOCKET;
  handshaken = false;
  in_len = 0;
  InterlockedIncrement(&session);
  emu_logf("[link] phone disconnected (%s)", why);
}

void ws_stop(void) {
  drop_client("power off");
  if (lsock != INVALID_SOCKET) closesocket(lsock);
  lsock = INVALID_SOCKET;
}

static bool send_all(const uint8_t *d, size_t n) {
  while (n && csock != INVALID_SOCKET) {
    int k = send(csock, (const char *)d, (int)n, 0);
    if (k == SOCKET_ERROR) {
      if (WSAGetLastError() != WSAEWOULDBLOCK) {
        drop_client("send failed");
        return false;
      }
      fd_set wf;
      FD_ZERO(&wf);
      FD_SET(csock, &wf);
      struct timeval tv = {1, 0};
      select(0, NULL, &wf, NULL, &tv);
      continue;
    }
    d += k;
    n -= (size_t)k;
  }
  return n == 0;
}

static void b64(const uint8_t *d, size_t n, char *o) {
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t i = 0;
  for (; i + 2 < n; i += 3) {
    uint32_t v = (uint32_t)d[i] << 16 | (uint32_t)d[i + 1] << 8 | d[i + 2];
    *o++ = T[v >> 18], *o++ = T[(v >> 12) & 63], *o++ = T[(v >> 6) & 63], *o++ = T[v & 63];
  }
  if (n - i == 1) {
    uint32_t v = (uint32_t)d[i] << 16;
    *o++ = T[v >> 18], *o++ = T[(v >> 12) & 63], *o++ = '=', *o++ = '=';
  } else if (n - i == 2) {
    uint32_t v = (uint32_t)d[i] << 16 | (uint32_t)d[i + 1] << 8;
    *o++ = T[v >> 18], *o++ = T[(v >> 12) & 63], *o++ = T[(v >> 6) & 63], *o++ = '=';
  }
  *o = 0;
}

/* HTTP upgrade request complete in `in`? Answer it. */
static void try_handshake(void) {
  in[in_len < INBUF ? in_len : INBUF - 1] = 0;
  char *end = strstr((char *)in, "\r\n\r\n");
  if (!end) {
    if (in_len >= INBUF - 1) drop_client("handshake too large");
    return;
  }
  char *k = NULL;
  for (char *p = (char *)in; p < end; p++) {
    if ((p == (char *)in || p[-1] == '\n') && !_strnicmp(p, "Sec-WebSocket-Key:", 18)) {
      k = p + 18;
      break;
    }
  }
  if (!k) {
    const char *r = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
    send_all((const uint8_t *)r, strlen(r));
    drop_client("not a WebSocket request");
    return;
  }
  while (*k == ' ') k++;
  char key[128];
  size_t kl = 0;
  while (k[kl] && k[kl] != '\r' && kl < 60) kl++;
  memcpy(key, k, kl);
  memcpy(key + kl, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36);
  uint8_t h[20];
  sha1_Raw((const uint8_t *)key, kl + 36, h);
  char acc[32], resp[256];
  b64(h, 20, acc);
  int n = snprintf(resp, sizeof(resp),
                   "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                   "Sec-WebSocket-Accept: %s\r\n\r\n",
                   acc);
  size_t used = (size_t)(end + 4 - (char *)in);
  memmove(in, in + used, in_len - used);
  in_len -= used;
  if (send_all((const uint8_t *)resp, (size_t)n)) {
    handshaken = true;
    InterlockedIncrement(&session);
    emu_logf("[link] phone connected from %s", peer);
  }
}

static void ws_send_frame(uint8_t opcode, const uint8_t *d, size_t n) {
  uint8_t h[4];
  size_t hl;
  h[0] = (uint8_t)(0x80 | opcode);
  if (n < 126) {
    h[1] = (uint8_t)n;
    hl = 2;
  } else {
    h[1] = 126, h[2] = (uint8_t)(n >> 8), h[3] = (uint8_t)n;
    hl = 4;
  }
  if (send_all(h, hl)) send_all(d, n);
}

void ws_poll_io(int timeout_ms) {
  fd_set rf;
  FD_ZERO(&rf);
  if (lsock != INVALID_SOCKET) FD_SET(lsock, &rf);
  if (csock != INVALID_SOCKET) FD_SET(csock, &rf);
  struct timeval tv = {0, timeout_ms * 1000};
  if (lsock == INVALID_SOCKET && csock == INVALID_SOCKET) {
    Sleep((DWORD)timeout_ms);
    return;
  }
  if (select(0, &rf, NULL, NULL, &tv) <= 0) return;
  if (lsock != INVALID_SOCKET && FD_ISSET(lsock, &rf)) {
    struct sockaddr_in a;
    int al = sizeof(a);
    SOCKET s = accept(lsock, (struct sockaddr *)&a, &al);
    if (s != INVALID_SOCKET) {
      if (csock != INVALID_SOCKET) drop_client("replaced by a new connection (one phone at a time)");
      csock = s;
      set_nonblocking(csock);
      BOOL nd = TRUE;
      setsockopt(csock, IPPROTO_TCP, TCP_NODELAY, (const char *)&nd, sizeof(nd));
      inet_ntop(AF_INET, &a.sin_addr, peer, sizeof(peer));
      handshaken = false;
      in_len = 0;
    }
  }
  if (csock != INVALID_SOCKET && FD_ISSET(csock, &rf)) {
    if (in_len >= INBUF) {
      drop_client("input overflow");
      return;
    }
    int k = recv(csock, (char *)in + in_len, (int)(INBUF - in_len), 0);
    if (k == 0) drop_client("closed");
    else if (k == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) drop_client("connection error");
    else if (k > 0) {
      in_len += (size_t)k;
      if (!handshaken) try_handshake();
    }
  }
}

bool ws_connected(void) { return csock != INVALID_SOCKET && handshaken; }
const char *ws_peer(void) { return peer; }

/* ---------------------------------------------------------------- HAL */
bool hal_link_connected(void) { return ws_connected(); }
uint32_t hal_link_session(void) { return (uint32_t)session; }
size_t hal_link_frame_size(void) { return FRAME_SIZE; }

size_t hal_link_recv(uint8_t *buf, size_t max) {
  for (;;) {
    if (!ws_connected() || in_len < 2) return 0;
    uint8_t op = in[0] & 0x0F;
    bool fin = in[0] & 0x80, masked = in[1] & 0x80;
    size_t len = in[1] & 0x7F, hl = 2;
    if (len == 126) {
      if (in_len < 4) return 0;
      len = (size_t)in[2] << 8 | in[3];
      hl = 4;
    } else if (len == 127) {
      drop_client("frame too large");
      return 0;
    }
    if (!masked || !fin || len > 1024) {
      drop_client("protocol violation");
      return 0;
    }
    if (in_len < hl + 4 + len) return 0;
    uint8_t *mask = in + hl, *p = in + hl + 4;
    for (size_t i = 0; i < len; i++) p[i] ^= mask[i & 3];
    size_t total = hl + 4 + len, out_n = 0;
    if (op == 0x2) { /* binary: one protocol frame */
      out_n = len <= max ? len : 0;
      if (out_n) memcpy(buf, p, len);
    } else if (op == 0x9) {
      ws_send_frame(0xA, p, len); /* ping -> pong */
    } else if (op == 0x8) {
      ws_send_frame(0x8, NULL, 0);
      drop_client("closed by app");
      return 0;
    }
    memmove(in, in + total, in_len - total);
    in_len -= total;
    if (out_n) return out_n;
  }
}

bool hal_link_send(const uint8_t *frame, size_t n) {
  if (!ws_connected()) return false;
  ws_send_frame(0x2, frame, n);
  return ws_connected();
}
