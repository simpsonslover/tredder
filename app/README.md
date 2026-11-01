# Phase 3: phone app (Expo SDK 58, React Native 0.88)

The phone side of the wallet. It shows balances and history, and builds transactions for the device to sign. It **never** holds keys, seeds, PINs, passphrases or backup passwords. There's no screen or code path that could accept them.

## Run it

The app uses Bluetooth, which needs native code. That means a **development build**, not Expo Go.

**Option A: build in the cloud, no Android Studio.**
1. `npx eas-cli@latest login` (free Expo account).
2. `npx eas-cli@latest build --profile development --platform android`. This takes about 15 min.
3. Install the APK on your phone from the link or QR code EAS shows.
4. On the PC, run `npx expo start`, then open the app on the phone. It connects to the dev server on the same Wi-Fi.

**Option B: build locally.**
1. Install Android Studio and its SDK.
2. Plug in the phone with USB debugging enabled.
3. Run `npx expo run:android`.

**iPhone (sideload, no Mac, no paid Apple account).**
1. On GitHub, open **Actions**, choose **iOS unsigned build**, then **Run workflow**. This uses a GitHub macOS machine and takes about 20–30 min.
   - **Release** (the default) builds a standalone app that opens by itself; no PC is needed. The simulator button is hidden, because `__DEV__` is false.
   - **Debug** builds a development client. It opens the Expo launcher and loads the JavaScript from Metro on your PC.
2. Download the artifact `tredder-Release-unsigned-ipa` (check the name: the `Debug` one opens the Expo launcher) from the finished run and unzip it.
3. Install [Sideloadly](https://sideloadly.io) on Windows, plug in the iPhone, drop in the `.ipa`, sign in with your Apple ID and press Start. Your Apple ID goes to Apple for signing; using a secondary Apple ID limits the risk.
4. On the iPhone:
   - Turn on **Settings → Privacy & Security → Developer Mode**.
   - Trust your Apple ID under **Settings → General → VPN & Device Management**.
5. For **Debug** builds, run `npx expo start --dev-client` on the PC. In the app, enter `http://<PC LAN IP>:8081`. Windows Firewall must allow Node on private networks.

Limits of a free Apple ID: the app stops opening after **7 days** until you sign it again with Sideloadly, and only 3 sideloaded apps can be installed at once. The `.ipa` is unsigned, so check the `.sha256` file next to it before signing.

**Try it without hardware:**
1. Go to the **Device** tab.
2. Choose **Use mock device**.

The mock serves the public test wallet `abandon … about`. Its data is exported by the real firmware core (`firmware/tools/mock_export.c`). It has **no keys and refuses to sign**, so Send stops at "Sign on device".

## Checks

```powershell
npm test               # 127 jest tests
npm run typecheck      # tsc --noEmit
npx expo lint
npx expo-doctor        # 20/20
npx expo export --platform android   # full Metro bundle (verifies all libraries resolve on-device)
```

## What's in it

| Screen | Does |
|---|---|
| Portfolio | Balances on every chain, USD total, pull to refresh |
| Receive | Asks the device to **display** the address. The address and QR appear only after you confirm on the device, and the app checks it against its own derivation |
| Send | BTC / ETH / ERC-20 / SOL / SPL: build, **simulate**, review, sign on device, broadcast, explorer link |
| Activity | Transaction history per chain |
| Device | BLE scan and pair (numeric comparison), mock device, lock |
| Find accounts | After importing a seed on the device: scans every scheme and account, then you pick what to show. Recommends sweep mode |
| Sweep | Scan old seed → plan → **approve on device (PIN + Confirm)** → one tx at a time, each confirmed on-chain → resume after interruption → erase old seed |
| Settings | Testnet/mainnet (mainnet off until you opt in), dust threshold, forget accounts |

| Layer | Where | Notes |
|---|---|---|
| Device protocol | `src/protocol/` | [protocol/PROTOCOL.md](../protocol/PROTOCOL.md): BLE framing, CBOR messages, strict validation of every device reply |
| Transports | `src/transport/` | `ble.ts` (react-native-ble-plx), `mock.ts` |
| Bitcoin | `src/chains/btc*.ts` | Watch-only derivation from device account keys, BIP44 gap-limit scan, coin selection, PSBTs with key origins (so the **device** verifies inputs and change), signature check before broadcast |
| EVM | `src/chains/evm.ts` | viem: Ethereum, Base, BNB, Polygon, Arbitrum (+ testnets); ERC-20 discovery via Blockscout; `eth_estimateGas` simulation; unsigned EIP-1559 to device |
| Solana | `src/chains/sol.ts` | SOL + SPL + Token-2022, stake detection, `simulateTransaction`, v0 messages to device |
| Prices | `src/prices/` | CoinGecko → DexScreener (liquid pairs only) → Jupiter |
| Sweep | `src/sweep/` | Planner (pure) + resumable executor |

## Security decisions (and their trade-offs)

- **No plaintext to real hardware.** BLE pairing alone doesn't stop another app on the same Android phone from using the bond. Every message to a real device must go through the Noise channel (protocol §3), which arrives with firmware **M5**. Until then `DeviceClient` refuses insecure BLE. *Trade-off:* the app can't talk to hardware yet, only to the mock.
- **Trust nothing the device says without checking.** Every reply is shape-checked:
  - Receive addresses must equal the app's own derivation.
  - A signed PSBT must be the exact transaction that was sent.
  - Signatures are verified before broadcast.
- **The device verifies the app too.** PSBT change outputs carry key origins, EVM/Solana sign requests name the key, and sweep signatures carry the approved plan step.
- **Simulate before signing.** Every send and every sweep step is simulated. In a sweep, tokens that fail simulation (transfer taxes, blacklists) move to the "handle manually" list.
- **Testnets first.** Mainnet is disabled until you enable it in Settings and accept a warning.
- **Privacy:**
  - Public account data lives in the OS secure store and is excluded from Android backups (`allowBackup: false`).
  - *Trade-off:* balances come from public APIs (mempool.space, Blockscout, public RPCs, CoinGecko, DexScreener, Jupiter), which can link your addresses together. Running your own nodes would fix that, but it's out of scope for now.

## Known limitations (honest list)

- **Token discovery:** BNB Chain and Polygon Amoy have no free Blockscout instance, so only native balances are discovered there. Tokens on those chains need a token list or a keyed indexer later.
- **Solana:**
  - Activity shows signatures and status, but not direction or amount.
  - Token symbols are shortened mint addresses until a token list is added.
- **NFTs and LP positions** are detected and listed but never moved, as the spec requires.
- **Coverage:** network-dependent code (live balances, broadcasting) is exercised against mocks and real libraries, but not yet against live testnets from a phone. That needs the dev build and, for signing, the firmware.
