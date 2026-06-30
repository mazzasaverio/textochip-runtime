# textochip-runtime

**A tiny, portable bytecode VM and mission library for microcontrollers, built on Zephyr.**
The open runtime behind [Text to Chip](https://textochip.com): it runs on **ESP32-S3** and
**Nordic nRF54L** today, and ports to other boards through a small **HAL** (`src/hal.h`, ~9
functions — the *only* file that differs per board).

A program is a flat list of **bytecode** (the ISA in [`SPEC.md`](SPEC.md)) that the browser
compiler produces and this VM executes. The **same bytecode** runs identically in the in-browser
simulator and on the board, so the IDE and the firmware evolve independently behind that contract.

**Two kinds of mission** (see [`ARCHITECTURE.md`](ARCHITECTURE.md)):
- **bytecode** — simple LED/buzzer/button behaviours the compiler *expands to bytecode* (e.g. the
  traffic light). They need **no code here** and run on the VM directly, in the browser too.
- **native C++** — richer behaviours kept here as a `MISSION` library invoked by `CALL` (e.g. the
  playable keyboard `pianola.h`). A `registry.cpp` dispatches `CALL <NAME>` to them.

A mission has a **beginning and an end** (it runs once, then control falls through); *a robot is a
sequence of missions*, looped with `GOTO`.

> **Open core.** This runtime is the open part of Text to Chip (Apache-2.0 when it goes public).
> The product keeps the browser IDE + compiler + simulator, the curated mission catalog (the
> manifest of tweakable guardrails), the AI assistant ([textochip-api](https://github.com/mazzasaverio/textochip-api)),
> the edge-AI model lifecycle ([textochip-ml](https://github.com/mazzasaverio/textochip-ml) — trains the
> on-device models this runtime executes), and the hardware kits. The bytecode ISA + serial protocol
> ([`SPEC.md`](SPEC.md)) is the stable contract between the four repos.

> **Verified on real ESP32-S3 hardware** over Web Serial from the IDE — PING/PONG, load/run, native
> MISSION with parameters, buzzer via LEDC PWM, button. Zephyr is the single runtime; the nRF54L
> port is next.

## Layout

```
src/      portable core (shared by ALL targets) — DO NOT put hardware calls here
  hal.h        the HAL interface (the only thing each board implements)
  isa.{h,cpp}  opcode parsing (std::string)
  vm.{h,cpp}   the tick-based bytecode VM
  mission.h, semaforo.h, registry.cpp   native MISSION libraries (e.g. SEMAFORO)
  runtime.{h,cpp}  serial protocol (PING/LOAD/RUN/STOP/OVERRIDE) + VM driver
host/     PC build (plain g++) — runs the VM with no board, for fast verification
  hal_host.cpp   HAL on the PC: simulated pins + clock, serial = stdout
  main.cpp       demo: semaforo bytecode, MISSION "SEMAFORO", and MISSION ... WITH params
zephyr/   Zephyr build (nRF Connect SDK for nRF54L, upstream Zephyr for ESP32)
  src/hal_zephyr.cpp   HAL on Zephyr (gpio_* / pwm / uart / k_uptime)
  CMakeLists.txt, prj.conf, app.overlay, src/main.cpp
```

## Build & run on the PC (works now — no toolchain needed)

```bash
cd host
make run
```

Expected: the traffic-light cycle prints with timestamps (red 5s → yellow 1s →
green 5s → loop), then the native `MISSION "SEMAFORO"` cycle with the walk-beep, and
a Demo 3 that exercises mission **parameters** (see below). This proves the VM is
fully platform-agnostic behind the Zephyr HAL. ✓ (verified)

### Edge-AI host tests (`docs/edge-ai.md`)

```bash
cd host
make test-ai      # the C MFCC matches textochip-ml's Python golden vectors
make test-ai-vm   # the firmware VM runs AISTART/INFER and branches on the AI class
make tflm-lib     # one-time: build libtensorflow-microlite.a from the submodule
make ai-infer     # end-to-end: TTS speech -> features.c -> TFLM -> the right word
```

### Mission parameters (honored in the firmware)

The native `Semaforo` mission now accepts the IDE's parameter form:

```
MISSION "SEMAFORO" WITH green=.. yellow=.. red=.. beep=off|slow|fast button=on|off minGreen=..
```

`Semaforo::setParams()` parses these and **clamps them to the guardrails**. The host build's
Demo 3 drives this path end-to-end.

## Build for hardware with Zephyr

For the **ESP32-S3** (works today): an **upstream Zephyr** workspace + the Zephyr SDK toolchain.
For the **nRF54L** (incoming): the **nRF Connect SDK** (Nordic's Zephyr + BLE + the nRF54L board) —
it reuses the same Zephyr SDK. From this folder:

```bash
# ESP32-S3 via upstream Zephyr (runs today)
west build -b esp32s3_devkitc/esp32s3/procpu zephyr
west flash

# nRF54LM20 DK  (incoming — verify the exact board string with: west boards | grep 54lm20)
west build -b nrf54lm20dk/nrf54lm20a/cpuapp zephyr
west flash
```

Then connect from the IDE (Real board) exactly as today — same protocol.

### Two USB ports, two roles (ESP32-S3)

The Freenove ESP32-S3 exposes **two** USB-C ports; the Zephyr setup uses each for a
different job — do not swap them:

- **Flash** via the **"USB UART"** port (CH343 chip, VID `1a86`) with `west flash`.
  esptool's DTR/RTS auto-reset into download mode works on this port.
- **Connect the IDE / Web Serial** via the **"USB OTG"** (native-USB) port. The app
  brings up a CDC-ACM device there that enumerates with VID `2fe3` as
  `CDC_ACM_serial_backend` — that is the port you pick in the browser's Web Serial chooser.

When reflashing while the CDC app is already running, esptool may not be able to auto-reset
into download mode — force it manually: hold **BOOT**, tap **RST**, release **BOOT**, then
`west flash`.

### USB CDC-ACM console (why the IDE talks to the native-USB port)

The app provides its **own USB CDC-ACM console on the USB-OTG controller** rather than using the
default UART0 console. In `zephyr/prj.conf`:

```
CONFIG_USB_DEVICE_STACK_NEXT=y
CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=y
```

and `zephyr/app.overlay` re-points `zephyr,console` / `zephyr,shell-uart` to a `cdc_acm_uart0`
node on `zephyr_udc0`.

**Why:** the default console is UART0, exposed through the board's CH343 "USB UART" port, whose
DTR/RTS lines drive an **auto-reset circuit**. A browser opening the port over Web Serial toggles
DTR/RTS, which **resets the chip** → the board never answers. An app-provided CDC-ACM on the
native "USB OTG" port has **no reset-on-DTR**, so the IDE can open it and get a stable
PING/PONG (this mirrors the Arduino "USB CDC On Boot" behavior).

### Board-specific notes (the only things to tune)
- **Pin numbers**: the bytecode bakes RAW pin numbers from `lib/boardProfile.ts`.
  `hal_zephyr.cpp` maps `raw = port*32 + pin` → `gpio0`/`gpio1`. Adjust the board
  profile to the GPIOs you wire (e.g. nRF P0/P1).
- **Buzzer (PWM): ✅ wired for the ESP32-S3.** `hal::tone` drives the buzzer over a
  **LEDC PWM** channel. `app.overlay` defines a `pwm0: &ledc0` node with a
  `ledc0_default` pinctrl group on **GPIO 5** (`LEDC_CH0_GPIO5`) and one channel.
  The `Semaforo` mission therefore does **not** `gpio-configure` the buzzer pin —
  the PWM peripheral owns it (driving it as a plain GPIO would kill the square
  wave). For another board, repoint the pinctrl group to your buzzer GPIO.
- **std::string** needs a full libc + heap → already set in `prj.conf`
  (`CONFIG_REQUIRES_FULL_LIBC`, `CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE`).

## Status
- ✅ Portable core + host build: **done and verified** (`make run` — semaforo + MISSION + mission
  params run on the PC).
- ✅ **Zephyr build for ESP32-S3: green** (`west build -b esp32s3_devkitc/esp32s3/procpu`,
  FLASH ~221 KB) with upstream Zephyr + Zephyr SDK 1.0.1. Config baked into `prj.conf`:
  `CONFIG_GLIBCXX_LIBCPP=y` (full C++ stdlib for `std::string`) plus the app-provided USB CDC-ACM
  console (`CONFIG_USB_DEVICE_STACK_NEXT=y`, `CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=y`); needs
  `west packages pip --install` (esptool ≥ 5.0.2) and the venv **activated** so esptool is on
  `PATH` at flash time.
- ✅ **Runs on real ESP32-S3 hardware**: flashed via the CH343 "USB UART" port, then driven from
  the IDE over Web Serial on the native-USB CDC-ACM port — PING/PONG, load/run, and the native
  MISSION **with parameters** all verified end-to-end. (See "Two USB ports, two roles" and "USB
  CDC-ACM console" above.)
- ✅ **Buzzer + button on the SEMAFORO mission**: the walk-beep plays through the **LEDC PWM**
  (GPIO 5) on real hardware, and a button press during green gives **immediate feedback** — the
  green LED blinks to acknowledge the crossing request, then the light turns red once green has
  lasted at least `minGreen`.
- ✅ **Edge-AI (voice keyword spotting) — implemented and host-proven through Phase 1.** The C
  feature extractor matches training exactly (`make test-ai`), the firmware VM runs the
  `AISTART`/`INFER` opcodes (`make test-ai-vm`), and `ai_infer` (TFLite Micro, a `third_party`
  submodule) classifies a real synthetic-speech model — `go/left/right/stop`, trained from Piper
  TTS with **zero recording** — end-to-end (`make ai-infer`). Only the on-board mic capture +
  service remain. Design + contract: [`docs/edge-ai.md`](docs/edge-ai.md); model lifecycle:
  [textochip-ml](https://github.com/mazzasaverio/textochip-ml).
- ⏳ Next: the **nRF54LM20 DK** (`-b nrf54lm20dk/...`) when the kits arrive (CMSIS-NN on the M33,
  the Axon NPU as the optional accelerator), then BLE OVERRIDE, and edge-AI on real hardware.
