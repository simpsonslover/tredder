# tredder device simulator (development only)

A desktop stand-in for the hardware wallet. It speaks **protocol v1** ([../protocol/PROTOCOL.md](../protocol/PROTOCOL.md)) with the same frames and CBOR messages as the device's BLE link, carried over a WebSocket. It reuses the app's own framing and codec code, so the two can't drift apart.

```powershell
cd sim; npm install
npx tsx src/server.ts                      # protocol ws://localhost:8765, device window http://localhost:8766
npx tsx src/server.ts --ephemeral --seed "abandon abandon ... about" --pin 1234   # throwaway test wallet
npm test                                   # 52 tests (e2e also against the firmware emulator, if built)
```

`tools/devstack/devstack.ps1` starts it automatically, next to the Android emulator.

## What it simulates

- **Screens and buttons:**
  - Up / Down / Back / OK in the device window.
  - Keyboard: arrows, Enter, Esc, typing letters or digits.
- **Setup:**
  - Create a wallet: shows the words, then a 3-word quiz.
  - Import 12/15/18/21/24 words with the **letter picker** (only letters that lead to a real BIP39 word are offered, with autocomplete). The checksum is checked, every word is reviewed, then you get the "was it ever typed?" warning with **Import & sweep** or **Import as-is**.
- **PIN:**
  - The digit order is randomised and reshuffled after every digit.
  - After 3 wrong PINs, delays start increasing.
  - **10 wrong PINs wipe the device.**
- **Pairing:** a 6-digit code, PIN, then Confirm. One phone at a time.
- **Auto-lock:** after 5 min idle and when the app disconnects.
- **Every protocol request**, with the security rules the real firmware must enforce:
  - **Bitcoin:**
    - Every input must be this wallet's key (fingerprint and path re-derived).
    - Change counts as change only when the device derives it itself.
    - Shows outputs and fee, and warns when the fee is above 10%.
  - **EVM:**
    - Decodes ERC-20 transfer and approve, with a **loud unlimited-approval warning**.
    - Refuses undecodable calls (blind signing off by default), and a chainId mismatch.
  - **Solana:**
    - The fee payer must be this account.
    - Decodes SOL and SPL transfers and token-account creation.
    - Refuses unknown programs.
  - **Sweep:**
    - The plan may only pay the device's **own** new wallet.
    - Each sweep signature must match an approved plan step.
    - The old seed can be erased.
- **Review pages:** OK only counts on the **last** page, so you have to scroll through everything.

## What it is NOT

- **Not the real firmware.** For that, use the **tredder emulator** ([firmware/platform/emu](../firmware/platform/emu/README.md)): the firmware's own C code on emulated hardware. The e2e tests run against both. This simulator doesn't run the RP2350 binary, and it isn't an instruction-level emulator. The firmware's PIN, storage, signing and BLE milestones (M2 to M5) don't exist yet. This simulates the **behaviour** the firmware must have, and its key derivation is tested against data exported by the real firmware core (`test/keys.test.ts`).
- **Not secure.** The seed and PIN are stored **in plain JSON** at `~/.tredder-sim/state.json` (`--ephemeral` keeps nothing on disk). Use public test phrases and testnets only.
- **Plaintext link:** the WebSocket link has no Noise encryption. The app only allows the simulator in development builds.
