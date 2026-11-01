#ifndef APP_PROTO_H
#define APP_PROTO_H

/* Protocol v1 (protocol/PROTOCOL.md): framing, CBOR requests, approvals. */

#include <stdbool.h>
#include <stdint.h>

#define PROTO_MSG_MAX (48u * 1024u) /* largest request accepted (PSBT with previous txs) */

void proto_init(void);
void proto_poll(void);                 /* drain received frames, handle requests */
void proto_link_changed(bool connected);
void proto_locked(void);               /* device locked: abort any approval, send "locked" event */
void proto_forget_pairing(void);       /* wipe / new wallet */
bool proto_paired(void);
bool proto_waiting_user(void);       /* a request is waiting for the user on screen */

#endif
