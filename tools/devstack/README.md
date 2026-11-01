# devstack: Android emulator + Expo app + tredder emulator (real firmware), wired together

```powershell
powershell -ExecutionPolicy Bypass -File tools\devstack\devstack.ps1
```

```
 ┌────────────── PC ──────────────────────────────────────────────────────┐
 │  tredder emulator (real firmware)      Android emulator "tredder_phone" │
 │   ws://localhost:8765  ◄── adb reverse tcp:8765 ──  Expo dev build      │
 │   firmware/build-emu/tredder-emu.exe                ("Connect to        │
 │                                                       tredder simulator")│
 │  Metro bundler :8081   ◄── adb reverse tcp:8081 ──  (JS bundle)          │
 └─────────────────────────────────────────────────────────────────────────┘
```

## First run (about 20–40 min, mostly downloads and the first Gradle build)

1. **Android SDK:** installed into `%LOCALAPPDATA%\Android\Sdk`. No admin needed. It uses Google's new `android` CLI with `--no-metrics`.
2. **Acceleration:**
   - The emulator needs a hypervisor. If none is usable, the script installs the **Android Emulator Hypervisor Driver** and Windows shows **one administrator prompt**. Approve it.
   - Alternative: enable *Windows Hypervisor Platform* under "Turn Windows features on or off", reboot, and re-run.
3. **Virtual phone:** creates the AVD `tredder_phone` (Pixel 7, Android 16 / API 36, x86_64).
4. **tredder device:** builds `firmware/build-emu/tredder-emu.exe` when firmware sources changed (Python + MSYS2 gcc), then starts it. It is the real firmware on emulated hardware; see [firmware/platform/emu/README.md](../../firmware/platform/emu/README.md). `-TsSimulator` uses the older browser-based TypeScript simulator instead.
5. **Emulator:** boots it, then runs `adb reverse` for the simulator and for Metro.
6. **App:** Gradle builds the development client (the first run downloads the NDK), then it is installed with `adb`. Metro starts in its own window, and the app opens on the phone.

**Later runs:** add `-SkipBuild` to reuse the installed app. Other flags: `-SetupOnly`, `-NoEmulator` (simulator only, e.g. for a USB phone with `adb reverse`), `-Headless`.

**Memory:** with 15 GB RAM, close large programs (games especially) before the first build. The emulator takes about 3 GB and Gradle 2–4 GB.

## Using it

1. In the **tredder emulator** window, set up the wallet with its buttons. Either create one, or import the public test phrase `abandon abandon … about` with the letter picker (typing letters presses the buttons for you).
2. In the **app** (emulator), go to **Device**, then **Connect to tredder simulator**.
3. Approve the pairing on the tredder emulator (OK, then PIN).
4. If the device was locked, unlock it on the tredder emulator, then tap **Reload** in the app. The device locks itself when the app disconnects (for example on a Metro reload) and after 5 minutes idle.
5. Try these:
   - **Receive:** the address appears in the app only after you confirm it on the device window.
   - **Send** (testnets): build and simulate in the app, review every page on the tredder emulator, then OK. The app checks the signature and broadcasts.
   - **Sweep:** import a seed on the device with **Import & sweep**. Then in the app: Device → Sweep → scan, approve the plan with PIN, and watch each transaction confirm.
