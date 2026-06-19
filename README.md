# firmware-zephyr — portable bytecode VM (host · ESP32 · nRF54L)

The **same** Sakura Board bytecode VM, one codebase, three targets. The VM / ISA /
missions are platform-agnostic and only ever call a tiny **HAL** (`src/hal.h`, ~9
functions). Each platform implements that HAL once — that is the *only* file that
differs per board. The ISA and the serial protocol are identical everywhere, so the
browser compiler and the same `.bas` → bytecode run unchanged on all three.

> This is the Zephyr port of the working Arduino firmware in `../firmware/ichigo_runtime/`.
> The Arduino version stays the reference until this one passes the same tests on hardware.

## Layout

```
src/      portable core (shared by ALL targets) — DO NOT put hardware calls here
  hal.h        the HAL interface (the only thing each board implements)
  isa.{h,cpp}  opcode parsing (Arduino String -> std::string)
  vm.{h,cpp}   the tick-based bytecode VM (logic identical to the Arduino vm.cpp)
  mission.h, semaforo.h, registry.cpp   native MISSION libraries (e.g. SEMAFORO)
  runtime.{h,cpp}  serial protocol (PING/LOAD/RUN/STOP/OVERRIDE) + VM driver
host/     PC build (plain g++) — runs the VM with no board, for fast verification
  hal_host.cpp   HAL on the PC: simulated pins + clock, serial = stdout
  main.cpp       demo: runs semaforo bytecode AND MISSION "SEMAFORO"
zephyr/   Zephyr build (nRF Connect SDK for nRF54L, upstream Zephyr for ESP32)
  src/hal_zephyr.cpp   HAL on Zephyr (gpio_* / pwm / uart / k_uptime)
  CMakeLists.txt, prj.conf, app.overlay, src/main.cpp
```

## Build & run on the PC (works now — no toolchain needed)

```bash
cd firmware-zephyr/host
make run
```

Expected: the traffic-light cycle prints with timestamps (red 5s → yellow 1s →
green 5s → loop), then the native `MISSION "SEMAFORO"` cycle with the walk-beep.
This proves the VM is off-Arduino and ready for the Zephyr HAL. ✓ (verified)

## Build for hardware with Zephyr

For the **ESP32-S3** (works today): an **upstream Zephyr** workspace + the Zephyr SDK toolchain.
For the **nRF54L** (incoming): the **nRF Connect SDK** (Nordic's Zephyr + BLE + the nRF54L board) —
it reuses the same Zephyr SDK. From this folder:

```bash
# nRF54LM20 DK  (verify the exact board string with: west boards | grep 54lm20)
west build -b nrf54lm20dk/nrf54lm20a/cpuapp zephyr
west flash

# ESP32-S3 via upstream Zephyr (same code, same toolchain)
west build -b esp32s3_devkitc/esp32s3/procpu zephyr
```

Then connect from the IDE (Real board) exactly as today — same protocol.

### Board-specific notes (the only things to tune)
- **Pin numbers**: the bytecode bakes RAW pin numbers from `lib/boardProfile.ts`.
  `hal_zephyr.cpp` maps `raw = port*32 + pin` → `gpio0`/`gpio1`. Adjust the board
  profile to the GPIOs you wire (e.g. nRF P0/P1).
- **Buzzer (PWM)**: `tone` needs a `pwm0` node — add it in `app.overlay` (LEDs,
  button, serial and timing already work without it).
- **std::string** needs a full libc + heap → already set in `prj.conf`
  (`CONFIG_REQUIRES_FULL_LIBC`, `CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE`).

## Status
- ✅ Portable core + host build: **done and verified** (`make run` — semaforo + MISSION run on the PC).
- ✅ **Zephyr build for ESP32-S3: green** (`west build -b esp32s3_devkitc/esp32s3/procpu`,
  FLASH ~154 KB) with upstream Zephyr + Zephyr SDK 1.0.1. Config baked into `prj.conf`:
  `CONFIG_GLIBCXX_LIBCPP=y` (full C++ stdlib for `std::string`); needs `west packages pip --install`
  (esptool ≥ 5.0.2) and the venv **activated** so esptool is on `PATH` at flash time.
- ⏳ Flash to hardware: pending — free the serial port first (the browser IDE / Web Serial holds
  it exclusively), then `west flash`.
- ⏳ Next: the **nRF54LM20 DK** (`-b nrf54lm20dk/...`) when the kits arrive, then BLE OVERRIDE as
  the Nordic-specific demo, and edge-AI missions on the Axon NPU.
