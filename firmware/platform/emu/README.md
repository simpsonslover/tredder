# tredder emulator: the real firmware on emulated hardware

```powershell
python firmware/tools/build_emu.py                  # -> firmware/build-emu/tredder-emu.exe (MSYS2 MinGW gcc)
firmware\build-emu\tredder-emu.exe                  # window; phone link ws://127.0.0.1:8765
```

The window is the rev A board drawn to scale from `hardware/tools/gen_pcb.py` (85.6 × 54 mm, top view), with the display module, Pico 2 W, microSD, USB-C, test pads and SW1–SW5 where they are on the PCB. The panel shows in the module's active area. Use `--scale 1.5` to set the size of one LCD pixel on the PC screen; by default it fits the screen.

`tools/devstack/devstack.ps1` builds it when sources change and starts it next to the Android emulator.

## What runs

The firmware is the same C code that will run on the RP2350:

- `firmware/app/`: the UI, PIN vault, protocol and main loop.
- `firmware/core/`: key derivation, plus PSBT, EVM and Solana parsing and signing.
- `firmware/third_party/`: trezor-crypto and libsecp256k1, compiled with the same security defines as the firmware.

Only the hardware layer under it (`app/include/app/hal.h`, `se.h`) is emulated:

| Board part | Emulation | Faithful to |
|---|---|---|
| ST7789 240×320 panel (SPI1), mounted landscape | `st7789_model.c` decodes the command stream (CASET/RASET/RAMWR/COLMOD/MADCTL/INVON/…) into the native portrait GRAM, then shows it as the glass lies on the board | The real driver's bytes. A missing `INVON` shows inverted colours, and a wrong `MADCTL` rotation shows the picture sideways or mirrored, as on the board. |
| BT1 LiPo + MCP73831 charger | `battery_model.c`: 500 mAh cell, VBAT on the "ADC", CHG_STAT, USB present. Click the USB-C port (J1) to plug or unplug the charger | The charger's 500 mA CC / 4.2 V CV and the sleep current from the circuit simulation (2.6 mA). The awake current (~53 mA + backlight) is an estimate. `--battery PCT`, `--battery-speed X`, `--usb`. |
| Display power (Q5) + sleep | `lcd_model_power`: unpowered = GRAM and settings lost. `hal_sleep`: the MCU waits for a button; the phone link closes (radio off) | SPI traffic to an unpowered module is logged as a driver bug. `--sleep-ms N` (default 60000, 0 = never). |
| SW1 power switch | Click it on the board: OFF halts the MCU and closes the link; ON restarts the process | RAM is lost and the flash and SE files are kept, so it is a real power cycle. It needs persistent state (not `--ephemeral`). |
| 4 buttons (GPIO 12–15) | Window buttons, keyboard, robot finger | Raw pin levels. The firmware debounces them itself. |
| NXP SE050 | `se_model.c`: PIN key, secret and attempt counter in `se050.bin` | The counter is incremented before the comparison, and 10 failures erase the secret. The I2C wire protocol is not modelled. |
| QSPI flash | `flash_model.c`, 2 × 4 KiB in `flash.bin` | NOR rules: erase sets bytes to 0xFF, and programming can only clear bits. |
| TRNG | OS CSPRNG raw bytes | The firmware's health tests still run on them. |
| BLE GATT | `ws_link.c`, a WebSocket server | Carries the same frames (244 bytes) as BLE. One phone at a time. |

State is kept in `%LOCALAPPDATA%\tredder-emu` (`--state DIR`). `--ephemeral` keeps everything in memory.

## Using it

- **Buttons:** click SW2–SW5 on the board (UP, DOWN, BACK, CONFIRM), or use the keys: arrows, Enter = CONFIRM, Esc/Backspace = BACK.
- **Typing:** a digit on the PIN pad or a letter in the word picker makes the emulator *press the buttons* that select it. The real device has only the 4 buttons.
- **Test options**, emulator only:
  - `--seed "public test words" --pin 1234 [--sweep-seed "…"]` provisions a wallet through the real vault code.
  - `--auto-approve` makes a robot finger approve every request.
  - `--debug-port N` opens a line protocol that reads the screen and presses buttons (`dbg.c`).
  - `--pin-delay-ms`, `--idle-lock-ms`.

## Tests (`cd sim; npm test`)

- **`test/e2e.test.ts`:** the app's real protocol client, PSBT builder and EVM/Solana code, run against both the TypeScript simulator and this emulator.
- **`test/emu-device.test.ts`:** on-device flows using buttons only:
  - PIN unlock, and the PIN pad reshuffling
  - the device wiping itself after 10 wrong PINs
  - letter-picker import and checksum repair
  - create plus the 3-word quiz
  - pairing
  - power cycling (the SE counter survives, and there is no plaintext in flash)

## What it is NOT

- It does not emulate the instruction set. The C code is compiled for x86-64, not executed as ARM Cortex-M33 code. Timing, the RP2350 boot ROM, secure boot, OTP and glitch resistance can only be tested on the board.
- It is not secure. `flash.bin` and `se050.bin` are ordinary files on the PC. Use public test phrases and testnets only.
- The phone link is not BLE. There is no LESC pairing and no Noise channel yet (M5), so the app only allows this link in development builds.
- `WL_DEBUGLINK` (screen readout for tests) is compiled into the emulator only. `ui.h` refuses to build it for ARM/RISC-V.
