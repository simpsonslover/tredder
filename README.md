# DIY hardware wallet

A personal-project hardware crypto wallet: RP2350 (Pico 2 W) + NXP SE050C2 secure element, with a colour screen, 4 buttons, microSD encrypted backups, BLE to a phone app, and a LiPo battery.

**Security is the priority. When convenience and security conflict, security wins, and the trade-off is written down.**

| Phase | Status | Where |
|---|---|---|
| 1. PCB (KiCad) | rev A routed, ERC/DRC clean, 19/19 circuit simulations pass, BOM | [hardware/](hardware/README.md) |
| 2. Firmware (Pico C SDK) | M1 wallet core done (8 347 host checks pass); M2 secure storage + PIN next | [firmware/](firmware/README.md) |
| 3. Mobile app (Expo dev build) | all screens built; 127 tests, lint/tsc/expo-doctor clean, Android bundle builds; talks to the mock device until firmware M5 adds the secure channel | [app/](app/README.md) |
| 4. Enclosure (tamper-evident) | not started | `enclosure/` |

Device ↔ app protocol: [protocol/PROTOCOL.md](protocol/PROTOCOL.md).

**Dev stack:** `tools/devstack/devstack.ps1` starts an Android emulator with the app, plus the [tredder device simulator](sim/README.md), and connects them ([how](tools/devstack/README.md)).

Every chain is built against testnets first (Bitcoin testnet, Sepolia, Solana devnet).
