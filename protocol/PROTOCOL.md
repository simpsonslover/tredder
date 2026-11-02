# Device ↔ app protocol, v1

Shared contract between the firmware (Phase 2, M5) and the phone app (Phase 3).
The app's implementation is in `app/src/protocol/`. The firmware will mirror it.

## Ground rules (from the security model)

- The app can only **request**. Every action that moves funds, reveals an address for receiving, or changes device state waits for a physical **Confirm** press. The screen shows everything the user is approving.
- Seeds, private keys, PINs, passphrases and backup passwords **never** appear in any message. There is no message type that could carry them, and the device has no code path that serialises them.
- The phone is untrusted:
  - **Hints:** anything the app sends as a "hint" (token symbol, decimals, contract name) is marked *unverified* on the device unless the device can check it itself, for example against a token list compiled into the firmware.
  - **Change outputs:** the device decides what counts as change. It derives the address itself and compares it with the outputs. It never takes the app's word for it.

## Layers

```
BLE GATT  ──►  framing (chunks ⇄ messages)  ──►  secure channel (Noise)  ──►  CBOR messages
```

### 1. BLE GATT

| | UUID | Properties |
|---|---|---|
| Service | `7a3f0001-5c1e-4c6b-9f2e-6b7d2a1e0b10` | |
| RX (app → device) | `7a3f0002-5c1e-4c6b-9f2e-6b7d2a1e0b10` | write (with response) |
| TX (device → app) | `7a3f0003-5c1e-4c6b-9f2e-6b7d2a1e0b10` | notify |

- **Pairing:** LE Secure Connections with **numeric comparison**. The same 6-digit code appears on the phone and on the device, and the user confirms on both.
- **One phone at a time:** the device stores a single bond. Pairing a new phone needs PIN + confirm, and replaces the old bond.

### 2. Framing

The ATT payload size is MTU − 3, at least 20 bytes. A message is split into frames:

| Frame | Layout |
|---|---|
| first | `0x80 \| last<<6`, then `len` (u32 big-endian, total message bytes), then payload |
| continuation | `0x00 \| last<<6`, then `seq` (u8, 1, 2, … wrapping to 1), then payload |

The receiver rejects the following and resets its reassembly state:
- a continuation frame without a first frame
- a sequence gap
- a declared length above **262 144** bytes
- more payload than declared
- a `last` flag set before all bytes have arrived

### 3. Secure channel (Noise) — implemented in M5, not yet

BLE encryption alone isn't enough. On Android, any app on the phone can use an existing bond. So every message is also protected end-to-end with the app instance's own key:

- **First pairing:** `Noise_XX_25519_ChaChaPoly_SHA256`. Afterwards the device shows a 6-digit code taken from the handshake hash, and the app shows the same code. The user confirms on the device. The device then stores the app's static public key, one app per device.
- **Every later session:** `Noise_KK_25519_ChaChaPoly_SHA256` with the stored keys. Messages from any other app or phone fail to decrypt and are dropped.
- **Where the keys live:**
  - **App:** static key in the phone's secure keystore (`expo-secure-store`), never exported.
  - **Device:** static key generated on the device and stored encrypted.

Until M5 ships, the app refuses to send any request over real BLE without this channel. Only the built-in **mock device** talks plaintext, and it holds no keys.

### 4. Messages (CBOR, string keys)

Request: `{ id: uint, type: string, ...params }`
Response: `{ id, ok: true, result }` or `{ id, ok: false, error: { code, message } }`
Event (device → app, no reply): `{ type: "event", event: "awaiting-user" | "locked" | "progress", id?, detail? }`

`awaiting-user` carries `detail: "pair"` while the device shows the pairing code (before the request itself); otherwise no detail.

| type | params | result | Device shows / needs |
|---|---|---|---|
| `get-info` | — | `{ model, firmware, protocol, initialized, locked, hasSweepSource }` | nothing |
| `get-accounts` | `wallet`, `net`, `scheme`, `accounts: uint[]` | `{ accounts: AccountKey[] }` | unlocked; no confirmation (public data) |
| `show-address` | `wallet`, `net`, `scheme`, `account`, `change`, `index` | `{ address, confirmed }` | address + QR on screen; user confirms it matches the app |
| `sign-btc` | `wallet`, `net`, `psbt: bytes`, `planStep?` | `{ psbt: bytes }` | every output, amount, fee, change detected by the device; Confirm |
| `sign-evm` | `wallet`, `scheme`, `account`, `chainId`, `tx: bytes` (unsigned EIP-1559), `hints?`, `planStep?` | `{ r, s, yParity }` | network, recipient, amount, fee, decoded call (ERC-20 transfer/approve; **unlimited approval warning**); blind signing off by default |
| `sign-sol` | `wallet`, `scheme`, `account`, `net`, `message: bytes`, `hints?`, `planStep?` | `{ signature: bytes, publicKey }` | decoded system/SPL transfers; anything undecodable is refused unless blind signing is enabled |
| `sweep-plan` | `plan` (see sweep below) | `{ approved, planHash }` | full summary: what moves, to which new addresses, total fee per chain; PIN + Confirm |
| `forget-sweep-source` | — | `{ erased }` | "Erase imported seed?" + PIN + Confirm; wipes the sweep-source seed |
| `lock` | — | `{}` | locks immediately |

- `wallet`: `"active"` (normal), or `"sweep-source"` (an imported seed being swept; only exists during a sweep).
- **Signing key:** for EVM and Solana, the request names the key as `scheme` + `account`. The device derives that address itself and refuses to sign if it isn't the transaction's sender/fee payer. For BTC, the PSBT's key-origin fields name the keys. The device checks the master fingerprint and paths against its own wallet.
- `planStep`: during a sweep, the index of the approved plan step this signature belongs to. Required when `wallet` is `"sweep-source"`.
- `net`: `"main"` or `"test"`.
- `scheme`: the firmware's scheme names: `btc-p2pkh`, `btc-p2sh-p2wpkh`, `btc-p2wpkh`, `btc-p2tr`, `evm-bip44`, `evm-ledger-live`, `sol-bip44-change`, `sol-bip44`, `sol-bip44-root`, `sol-cli`.

`AccountKey` is one of:
- `{ account, path, kind: "xpub", publicKey: bytes(33), chainCode: bytes(32), masterFingerprint: uint32 }`: BTC account level. The app derives receive and change addresses from it (watch-only).
- `{ account, path, kind: "address", address }`: EVM and Solana, where account paths are hardened.

Error codes: `USER_REJECTED`, `LOCKED`, `BUSY`, `UNSUPPORTED`, `INVALID_REQUEST`, `NOT_INITIALIZED`, `TIMEOUT`, `INTERNAL`.

### Sweep (device side, firmware M7)

1. **Import and back up:** the imported seed becomes `sweep-source`. The device generates a brand-new `active` seed and requires the backup quiz or a microSD backup first.
2. **Scan:** the app scans `sweep-source` through `get-accounts`, then sends `sweep-plan`, which lists per chain each transfer with asset, amount, destination and fee estimate.
3. **Approve:** the device checks that every destination is an address of its own `active` wallet. It then shows the summary, and the user approves it with PIN + Confirm.
4. **Sign:** each later `sign-*` request on `sweep-source` must match a step of the approved plan, by plan hash plus step index. Anything else is refused.
