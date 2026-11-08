# RP2350 (Raspberry Pi Pico 2 W) firmware build.
#
#   Requires: Pico SDK 2.x, arm-none-eabi GCC, CMake, Ninja (the "Raspberry Pi Pico"
#   VS Code extension installs all of these) and PICO_SDK_PATH set.
#
#   cmake -S firmware -B firmware/build -G Ninja -DPICO_BOARD=pico2_w
#   cmake --build firmware/build
#
# Host unit tests do not use this file: see tools/host_test.py.
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Board")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(tredder_wallet C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

# ---------------------------------------------------------------- configuration
# Keep in sync with DEFINES in tools/host_test.py (security-relevant!).
set(WALLET_DEFINES
    USE_BIP32_CACHE=0 USE_BIP39_CACHE=0
    USE_ETHEREUM=1 USE_KECCAK=1 USE_CARDANO=0 USE_MONERO=0 USE_NEM=0 USE_PRECOMPUTED_CP=0
    SECP256K1_STATIC ENABLE_MODULE_EXTRAKEYS=1 ENABLE_MODULE_SCHNORRSIG=1 ENABLE_MODULE_RECOVERY=1
    ECMULT_WINDOW_SIZE=2 COMB_BLOCKS=11 COMB_TEETH=6)

# ---------------------------------------------------------------- third-party crypto (unmodified)
set(TC ${CMAKE_CURRENT_LIST_DIR}/third_party/trezor-crypto)
add_library(trezor_crypto STATIC
    ${TC}/address.c ${TC}/base58.c ${TC}/bignum.c ${TC}/bip32.c ${TC}/bip39.c ${TC}/bip39_english.c
    ${TC}/blake256.c ${TC}/blake2b.c ${TC}/blake2s.c ${TC}/groestl.c ${TC}/ecdsa.c ${TC}/curves.c
    ${TC}/secp256k1.c ${TC}/nist256p1.c ${TC}/hasher.c ${TC}/hmac.c ${TC}/hmac_drbg.c ${TC}/rfc6979.c
    ${TC}/memzero.c ${TC}/consteq.c ${TC}/pbkdf2.c ${TC}/ripemd160.c ${TC}/sha2.c ${TC}/sha3.c
    ${TC}/segwit_addr.c ${TC}/rand.c
    ${TC}/ed25519-donna/ed25519.c ${TC}/ed25519-donna/ed25519-keccak.c ${TC}/ed25519-donna/ed25519-sha3.c
    ${TC}/ed25519-donna/curve25519-donna-32bit.c ${TC}/ed25519-donna/curve25519-donna-helpers.c
    ${TC}/ed25519-donna/curve25519-donna-scalarmult-base.c ${TC}/ed25519-donna/modm-donna-32bit.c
    ${TC}/ed25519-donna/ed25519-donna-basepoint-table.c ${TC}/ed25519-donna/ed25519-donna-32bit-tables.c
    ${TC}/ed25519-donna/ed25519-donna-impl-base.c
    ${TC}/chacha20poly1305/chacha20poly1305.c ${TC}/chacha20poly1305/chacha_merged.c
    ${TC}/chacha20poly1305/poly1305-donna.c ${TC}/chacha20poly1305/rfc7539.c)
target_include_directories(trezor_crypto PUBLIC ${TC})
target_compile_definitions(trezor_crypto PUBLIC ${WALLET_DEFINES})
target_compile_options(trezor_crypto PRIVATE -w)

set(SECP ${CMAKE_CURRENT_LIST_DIR}/third_party/secp256k1)
add_library(secp256k1 STATIC ${SECP}/src/secp256k1.c ${SECP}/src/precomputed_ecmult.c
    ${SECP}/src/precomputed_ecmult_gen.c)
target_include_directories(secp256k1 PUBLIC ${SECP}/include PRIVATE ${SECP}/src)
target_compile_definitions(secp256k1 PUBLIC ${WALLET_DEFINES})
target_compile_options(secp256k1 PRIVATE -w)

# Monocypher 4.0.2, Argon2id only (microSD backup password hash), unmodified.
set(MONO ${CMAKE_CURRENT_LIST_DIR}/third_party/monocypher)
add_library(monocypher STATIC ${MONO}/monocypher.c)
target_include_directories(monocypher PUBLIC ${MONO})
target_compile_options(monocypher PRIVATE -w)

# ---------------------------------------------------------------- wallet core (portable, host-tested)
file(GLOB WALLET_CORE_SRC ${CMAKE_CURRENT_LIST_DIR}/core/src/*.c)
add_library(wallet_core STATIC ${WALLET_CORE_SRC})
target_include_directories(wallet_core PUBLIC ${CMAKE_CURRENT_LIST_DIR}/core/include)
target_link_libraries(wallet_core PUBLIC trezor_crypto secp256k1 monocypher)
target_compile_options(wallet_core PRIVATE -Wall -Wextra -Werror -Wshadow -Wvla)

# ---------------------------------------------------------------- firmware image
add_executable(wallet
    platform/rp2350/main.c
    platform/rp2350/platform_hooks.c)
target_include_directories(wallet PRIVATE platform/rp2350)
target_link_libraries(wallet PRIVATE wallet_core pico_stdlib pico_rand)
target_compile_options(wallet PRIVATE -Wall -Wextra -Werror)
# Bring-up only: console on USB CDC. Production builds disable both (see README).
pico_enable_stdio_usb(wallet 1)
pico_enable_stdio_uart(wallet 0)
pico_add_extra_outputs(wallet)

# ---------------------------------------------------------------- bring-up: display + buttons
# No secrets, no secure element: tests the ST7789 wiring and the four buttons (breadboard or PCB).
add_executable(bringup_display
    platform/rp2350/bringup_display.c
    platform/rp2350/hal_display.c
    app/src/gfx.c
    app/src/font_data.c)
target_include_directories(bringup_display PRIVATE platform/rp2350 app/include)
target_link_libraries(bringup_display PRIVATE pico_stdlib hardware_spi)
target_compile_options(bringup_display PRIVATE -Wall -Wextra -Werror)
pico_enable_stdio_usb(bringup_display 1)
pico_enable_stdio_uart(bringup_display 0)
pico_add_extra_outputs(bringup_display)

# ---------------------------------------------------------------- microSD files: FatFs (unmodified)
# FAT12/16/32 + exFAT; configuration in app/fs/ffconf.h (no formatter on the device).
# PC tests: tools/fs_test.py.
set(FATFS ${CMAKE_CURRENT_LIST_DIR}/third_party/fatfs)
add_library(fatfs STATIC ${FATFS}/ff.c ${FATFS}/ffunicode.c)
target_include_directories(fatfs PUBLIC ${FATFS} ${CMAKE_CURRENT_LIST_DIR}/app/fs)
target_compile_options(fatfs PRIVATE -w)

# ---------------------------------------------------------------- the wallet firmware, DEMO without the SE050
# The real UI/vault/PIN code (app/) on the board, for the breadboard before the secure element is
# fitted. TREDDER_DEMO_NO_SE: storage and the "SE" are RAM-only (lost at reset), seed import is
# disabled. WL_PROTO_NOISE: BLE link with the Noise secure channel (M5); pairing is RAM-only too.
# Never use it with real funds. The secure build needs the SE050 driver (M2).
file(GLOB APP_SRC ${CMAKE_CURRENT_LIST_DIR}/app/src/*.c)
add_executable(tredder_demo
    platform/rp2350/main_os.c
    platform/rp2350/hal_display.c
    platform/rp2350/hal_board.c
    platform/rp2350/hal_ble.c
    platform/rp2350/se_demo.c
    platform/rp2350/sd_spi.c
    app/fs/sdfs.c
    app/fs/sdbackup.c
    ${APP_SRC})
target_include_directories(tredder_demo PRIVATE platform/rp2350 app/include)
target_compile_definitions(tredder_demo PRIVATE TREDDER_DEMO_NO_SE WL_PROTO_NOISE CYW43_LWIP=0 FW_VERSION="0.3.0-demo")
target_link_libraries(tredder_demo PRIVATE wallet_core fatfs pico_stdlib pico_rand hardware_spi hardware_adc
    pico_cyw43_arch_poll pico_btstack_ble pico_btstack_cyw43)
pico_btstack_make_gatt_header(tredder_demo PRIVATE ${CMAKE_CURRENT_LIST_DIR}/platform/rp2350/tredder.gatt)
# Strict warnings on our sources only (the SDK's INTERFACE libraries, e.g. BTstack, compile inside
# this target too and are not ours to fix).
set_source_files_properties(
    platform/rp2350/main_os.c platform/rp2350/hal_display.c platform/rp2350/hal_board.c
    platform/rp2350/hal_ble.c platform/rp2350/se_demo.c platform/rp2350/sd_spi.c app/fs/sdfs.c app/fs/sdbackup.c
    ${APP_SRC}
    TARGET_DIRECTORY tredder_demo PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-Wno-format-truncation")
pico_enable_stdio_usb(tredder_demo 1)
pico_enable_stdio_uart(tredder_demo 0)
pico_add_extra_outputs(tredder_demo)

# ---------------------------------------------------------------- bring-up: microSD (read-only)
add_executable(bringup_sd
    platform/rp2350/bringup_sd.c
    platform/rp2350/sd_spi.c
    platform/rp2350/hal_display.c
    app/fs/sdfs.c
    app/src/gfx.c
    app/src/font_data.c)
target_include_directories(bringup_sd PRIVATE platform/rp2350 app/include)
target_link_libraries(bringup_sd PRIVATE pico_stdlib hardware_spi fatfs)
target_compile_options(bringup_sd PRIVATE -Wall -Wextra -Werror)
pico_enable_stdio_usb(bringup_sd 1)
pico_enable_stdio_uart(bringup_sd 0)
pico_add_extra_outputs(bringup_sd)
