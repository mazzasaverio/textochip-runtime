# textochip-runtime

**A tiny, portable bytecode VM for microcontrollers, built on Zephyr.** The open runtime
behind [Text to Chip](https://textochip.com): it runs today on the **ESP32-S3** and the
**Nordic nRF54LM20 DK**, and ports to other boards through a small **HAL** (`src/hal.h`,
the only interface each board implements).

A program is a flat list of **bytecode** (the ISA in [`SPEC.md`](SPEC.md)) produced by the
browser compiler. The **same bytecode** runs identically in the in-browser simulator and on
the board, so the IDE and the firmware evolve independently behind that contract. The board
receives programs over a plain **serial protocol** (PING/LOAD/RUN/STOP/OVERRIDE/SAVE, also
in `SPEC.md`) and can persist one to flash and rerun it on boot, PC unplugged.

## Layout

```
src/        portable core, shared by ALL targets (no hardware calls here)
  hal.h            the HAL interface (the only thing each board implements)
  isa.{h,cpp}      opcode parsing (32 opcodes)
  vm.{h,cpp}       the tick-based bytecode VM
  runtime.{h,cpp}  serial protocol + VM driver
  ai/              edge-AI: mic capture -> the ai_service:: interface -> VOICE().
                   TWO interchangeable classifier backends (CMake picks one):
                     ai_service.cpp     MFCC (features.c) + int8 TFLite Micro  [host/ESP32/nRF54L-A]
                     ai_nrf_edgeai.cpp  Nordic nRF Edge AI on the Axon NPU      [nRF54LM20B]
  mission.h, semaforo.h, pianola.h, registry.cpp   legacy native missions (CALL)
host/       PC build (plain g++): run and test the VM with no board
zephyr/     Zephyr application build
  src/hal_zephyr.cpp   the HAL on Zephyr (gpio / pwm / uart / i2s / nvs)
  prj.conf             board-agnostic config
  boards/              per-board config + devicetree overlays (ESP32-S3, Nordic DK A + B)
  Kconfig              CONFIG_TEXTOCHIP_AI (edge-AI service) + _NRF_EDGEAI (Axon backend)
third_party/tflite-micro   submodule, only needed for the TFLM backend
```

**Voice is live on the Nordic DK.** `IF VOICE()="go" THEN MOVE 180 180` drives the robot's
wheels from a spoken word, verified end-to-end on the nRF54LM20 DK (INMP441 I2S mic on the TDM
peripheral). On the **B** chip the classifier is Nordic's Axon-NPU KWS model (~1-12 ms/inference,
trained on thousands of speakers); on the A chip / ESP32 it is our own MFCC + TFLite-Micro model.
Same `VOICE()` bytecode either way. See [`docs/edge-ai.md`](docs/edge-ai.md).

## Run on the PC (no toolchain needed beyond g++)

```bash
cd host
make run        # the traffic-light demo + legacy MISSION path, timestamped
```

Edge-AI host tests:

```bash
make test-ai        # the C MFCC matches textochip-ml's Python golden vectors
make test-ai-vm     # the VM runs AISTART/INFER and branches on the AI class
make tflm-lib       # one-time: git submodule update --init --depth 1 third_party/tflite-micro
make ai-infer       # end-to-end: speech samples in, the right word out
make test-vision    # same for SEE() with TFLM person-detection (object classes)
make test-color     # the colour-blob detector: RGB frame -> SEE()/SEEX()/SEESIZE()
make test-color-move # colour vision end-to-end: the robot hunts a ball on the VM, no camera
make color-probe IMG=photo.jpg   # what the board would SEE in a REAL photograph
make test-color-service # the colour vision SERVICE: camera stub -> chunked capture -> SEE() class
make test-dist      # DIST (ultrasonic): a fed distance -> DISTANCE() -> the robot stops within 10 cm
```

## Build for hardware

Both boards build from this folder with `west`. Per-board work lives only in
`zephyr/boards/` plus the pin map in `hal_zephyr.cpp`.

### ESP32-S3 (upstream Zephyr + Zephyr SDK)

```bash
west build -b esp32s3_devkitc/esp32s3/procpu zephyr
west flash      # via the "USB UART" port (CH343); needs esptool >= 5.0.2 on PATH
```

Two USB ports, two roles: **flash** on the "USB UART" port, **connect the IDE** on the
"USB OTG" port (the app provides a CDC-ACM console there, VID `2fe3`, because the UART
port's DTR/RTS auto-reset would reboot the chip every time the browser opens it; the
config lives in `zephyr/boards/esp32s3_*.conf` + `.overlay`). If flashing cannot
auto-reset: hold BOOT, tap RST, release BOOT, retry. The edge-AI tier is ON here
(`CONFIG_TEXTOCHIP_AI=y`: TFLM linked, voice model in the ELF).

### Nordic nRF54LM20 DK (nRF Connect SDK)

```bash
# nRF54LM20A: our own model (MFCC features.c + int8 TFLite Micro), like the ESP32.
west build -b nrf54lm20dk/nrf54lm20a/cpuapp zephyr
west flash -r jlink

# nRF54LM20B: voice on the Axon NPU via Nordic's nRF Edge AI Add-on. Check out the
# add-on (github.com/nrfconnect/sdk-edge-ai, v2.2.0) and pass it as a module:
west build -b nrf54lm20dk/nrf54lm20b/cpuapp zephyr \
    -- -DEXTRA_ZEPHYR_MODULES=$HOME/projects/sdk-edge-ai
west flash -r jlink
```

The console is the DK's on-board **J-Link VCOM** (115200, no reset-on-DTR): flash and
connect on the same USB cable. Use the **jlink runner**: plain `probe-rs` writes this
chip incompletely at times (upstream [probe-rs#3775](https://github.com/probe-rs/probe-rs/issues/3775));
the product's user installer works around it with verify + retry. Both builds have the
edge-AI tier ON (`CONFIG_TEXTOCHIP_AI=y`); the B build adds `CONFIG_TEXTOCHIP_NRF_EDGEAI=y`
to swap the classifier for Nordic's Axon model. Port notes,
bring-up log and pitfalls: [`docs/nordic-nrf-connect-sdk.md`](docs/nordic-nrf-connect-sdk.md).

## Status (bench-verified)

- **ESP32-S3**: the full product path on real hardware. Serial protocol, LEDs, buzzer
  (LEDC PWM), button, servo, ADC, SAVE + boot autorun, TFLM linked on-device.
- **Nordic nRF54LM20 DK**: serial protocol (interrupt-driven RX), on-board LEDs and
  button, buzzer PWM, the **L298N motors drive real wheels** (`MOVE`), **voice is
  live** (the INMP441 I2S mic on the TDM peripheral feeds VOICE(), with Nordic's Axon-NPU
  KWS model on the B chip, ~1 to 12 ms per inference), and **SAVE + boot autorun** is
  verified (a saved program reruns on power-up, PC unplugged). Note: the DK persists to
  RRAM via `flash_area`, not NVS (NVS does not stick on this no-explicit-erase RRAM).
- The VM zeroes motors and buzzer on every stop path (STOP, HALT, end, error): a robot
  must not keep rolling after STOP.

## The four repos

| Repo | What it is |
|---|---|
| [`textochip`](https://github.com/mazzasaverio/textochip) | The product: Next.js IDE, Chip BASIC compiler, simulator, landing |
| **`textochip-runtime`** (this one) | The firmware: bytecode VM on Zephyr. Open core (Apache-2.0 when public) |
| [`textochip-api`](https://github.com/mazzasaverio/textochip-api) | The AI assistant backend: natural language to Chip BASIC |
| [`textochip-ml`](https://github.com/mazzasaverio/textochip-ml) | The edge-AI model lifecycle: trains the int8 models this runtime executes |

## Documentation

Docs stay fresh with every change: living docs (this README, `SPEC.md`, `ARCHITECTURE.md`,
the runbook) are updated in place; log docs (the Nordic bring-up notes) get dated appended
entries.

- [`SPEC.md`](SPEC.md): the bytecode ISA + serial protocol, the contract with the product repo. Keep them in sync.
- [`ARCHITECTURE.md`](ARCHITECTURE.md): how the core, HAL and boards fit together.
- [`docs/bench-runbook.md`](docs/bench-runbook.md): wiring + stage-by-stage bring-up of the voice robot.
- [`docs/edge-ai.md`](docs/edge-ai.md): the VOICE()/SEE() inference design.
- [`docs/nordic-nrf-connect-sdk.md`](docs/nordic-nrf-connect-sdk.md): the Nordic port, living notes.

Legacy note: composable `MISSION "X"`/`CALL` blocks are retired in the product (a mission
is one standalone reactive program since 2026-07-11), but the `CALL` opcode and the native
mission registry stay here so previously saved bytecode keeps running.
