# Nordic nRF Connect SDK — learning notes (living doc)

> **Living document.** Notes captured while learning the Nordic **nRF Connect SDK (NCS)** for the
> **nRF54LM20 DK** kit, added incrementally as material comes in. Each lesson is historicised
> verbatim-ish, followed by **how it maps to textochip** (what it means for our runtime). Newest
> lessons appended at the bottom; the "How this maps to textochip" boxes are the actionable part.

---

## Lesson 1 — nRF Connect SDK introduction

### Overview

The **nRF Connect SDK (NCS)** is a scalable, unified SDK for building low-power wireless
applications on Nordic Semiconductor's **nRF54 / nRF53 / nRF52 / nRF70 / nRF91** Series devices. It
scales from size-optimised software for memory-constrained parts to complex applications on more
capable devices, and is **publicly hosted on GitHub**.

It integrates the **Zephyr RTOS** plus a wide range of applications, samples, and protocol stacks:
Bluetooth Low Energy, Bluetooth mesh, Wi-Fi, Matter, Thread/Zigbee, LTE-M/NB-IoT/GPS, TCP/IP. It
also bundles middleware (CoAP, MQTT, LwM2M), libraries, hardware drivers, **Trusted Firmware-M**
(security), and a secure bootloader (**MCUboot**).

**Zephyr RTOS** is an open-source real-time OS for connected, resource-constrained embedded
devices: a deterministic scheduler, a rich set of libraries/middleware, and highly configurable,
scalable builds — from ~8 KB (a simple LED blink) up to multi-MB, feature-rich configurations.

The NCS offers a **single code base** for all of Nordic's devices and software components, so
porting modules/libraries/drivers between applications is simpler. You pick only the components
your application needs (high memory efficiency).

### Architecture (layered)

```
┌───────────────────────────────────────────────┐
│  Nordic Applications                            │
├───────────────────────────────────────────────┤
│  Connectivity · Protocols · Security · Libraries · DFU │
├───────────────────────────────────────────────┤
│                  Middleware                     │
├─────────────────────────┬─────────────────────┤
│      Zephyr RTOS         │  Wireless Stacks &   │
│                          │  Peripheral Drivers  │
├─────────────────────────┴─────────────────────┤
│               Board Configuration               │
└───────────────────────────────────────────────┘
```

Zephyr RTOS and third-party components (e.g. **MCUboot**, **Trusted Firmware-M**) are the
open-source / dark-blue layers; Nordic's applications and stacks are the light-blue layers.

### The four repositories NCS is organised into

| Repo | What | Who |
|---|---|---|
| **nrf** | Applications, samples, connectivity protocols | Nordic |
| **nrfxlib** | Common libraries and stacks | Nordic |
| **Zephyr** | RTOS & board configurations | **open source** |
| **MCUboot** | Secure bootloader | **open source** |

### Exercises in the lesson (for reference)

1. **Exercise 1 — Install NCS + VS Code**: download/install the nRF Connect SDK with the VS Code
   IDE + the **nRF Connect for VS Code** extension pack. `west` is the core command-line utility.
2. **Exercise 2 — Build your first app (blinky)**: build + flash `blinky` (toggles an LED on the
   board), then modify its source to blink at a different frequency and rebuild.

### How this maps to textochip

> **Q (Saverio): is this the SAME Zephyr we're already using for the ESP32-S3?**
> **Yes.** NCS is **Nordic's distribution _of_ Zephyr** — it is Zephyr RTOS **plus** Nordic's
> add-ons (`nrf`, `nrfxlib`) + MCUboot + TF-M, glued by the same **`west`** workspace tool. So:
> - **ESP32-S3** → we build against **upstream Zephyr** (`west build -b esp32s3_devkitc/esp32s3/procpu zephyr`).
> - **nRF54LM20 DK** → we build against **NCS** (`west build -b nrf54lm20dk/nrf54lm20a/cpuapp`), which
>   IS Zephyr + Nordic modules.
>
> Same RTOS, same `west`, the **same devicetree + Kconfig** model, the same driver/HAL pattern.
> This is exactly **why we unified textochip's firmware on Zephyr**: one runtime, one build system,
> two boards. Concretely, for textochip nothing structural changes across the two chips —
> - the **bytecode ISA / VM / missions / the browser compiler are identical** (the ISA is the stable
>   contract);
> - **per-board work = one HAL file + the board profile** (`src/hal.h` impl per board;
>   `lib/board3d`/`lib/boardProfile.ts` on the product side);
> - Nordic's extra value (BLE, the **Axon NPU** on the nRF54LM20B for edge-AI, TF-M/MCUboot) is
>   opt-in on top — see [`edge-ai.md`](edge-ai.md).
>
> Practical note: on the ESP32-S3 our console is an app-provided USB CDC-ACM (Web Serial needs it);
> the nRF54LM20 DK has an on-board debugger/J-Link with a virtual COM port, so flashing + console are
> one USB-C cable (the `blinky` flow above). Bring-up details will land in
> [`bench-runbook.md`](bench-runbook.md) as we go.

---

## Workspace strategy (decided 2026-07-11)

Two questions came up starting the NCS course; the answers are project policy now.

**Do we use NCS for the ESP32-S3 too, to unify?** **No.** NCS is Nordic-curated: the ESP32-S3
lives on **Espressif's HAL in *upstream* Zephyr**, which Espressif develops/tests there, not
against Nordic's downstream fork. Building the ESP32 under NCS puts you off *both* vendors'
supported paths and into version-skew (NCS pins its own Zephyr). And it buys nothing: our VM code
doesn't care whether the RTOS under it is "upstream Zephyr" or "NCS Zephyr" — it's Zephyr either
way, and our code is already portable across both (board string + one HAL file). **Policy:
ESP32-S3 → upstream Zephyr; nRF54L → NCS.** They share the same **Zephyr SDK toolchain** (installed
once), so the only extra cost is two `west` source trees. Reconsider only if the ESP32 is ever
dropped as a target.

**Separate west workspace for the course?** **Yes — and let Nordic's tooling create it.** Install
NCS via the **nRF Connect for VS Code extension + Toolchain Manager** (what the Nordic Developer
Academy assumes); it puts a managed NCS install (e.g. `~/ncs/v<version>/`) in its own directory,
which *is* a west workspace, physically separate from our upstream-Zephyr workspace. Reasons:
version isolation (the course targets a specific NCS version; when we do the textochip nRF54L
bring-up we'll pin our own — separate workspaces = no `west update` fights), and freedom to break
things while learning. **Workspace ≠ app:** the NCS install is the workspace; an app (a course
exercise, or `textochip-runtime`) is just a folder you `west build` against it. When we reach the
Nordic bring-up we can build the runtime as a freestanding app against the *same* NCS install if
the versions line up, or a dedicated one if they diverge.

**Version to install (decided 2026-07-11, from Lesson 1 Exercise 1).** Target the **latest LTS in
the v3.4.x series** (the first NCS LTS; use the highest minor shown, e.g. `v3.4.2`, not `v3.4.0` —
LTS is a branch of tags, always take the newest). Install via *Install SDK* (the bundled
SDK + toolchain + nrfutil, not *Install Toolchain*), SDK type *nRF Connect SDK* (the full
Zephyr-based one, NOT *Bare Metal*). **When we build `textochip-runtime` for the nRF54LM20 DK, pin
the SAME v3.4.x LTS** the course installed — so the runtime and the learning workspace never fight
over versions. Linux prerequisites the course installs first: SEGGER **J-Link** + **nrf-udev**
(the udev rules need a re-login or a board re-plug to take effect).

<!-- Append the next lesson's notes below this line, same shape:
## Lesson N — <title>
### … (the material)
### How this maps to textochip
-->

## Course progress (updated 2026-07-12)

- **Installed:** NCS **v3.4.0** + toolchain via the VS Code extension (managed install at
  `~/ncs/v3.4.0/`, toolchains at `~/ncs/toolchains/`). J-Link + nrf-udev in place.
- **Lesson 1 done on hardware:** blinky built for `nrf54lm20dk/nrf54lm20a/cpuapp` and flashed —
  LED blinks. (App folder: `~/blinky`; created via the extension's "Create a new application".)
- **Lessons 2–8 done as THEORY ONLY** (decided: Saverio reads the theory and maps it to textochip;
  the hands-on exercises are skipped — our HAL work later IS the exercise). Course exercise repo
  cloned at `~/ncs/ncsfund` (NordicDeveloperAcademy/ncs-fund) if ever needed.
- **Course status: THEORY COMPLETE (L1–L8, 2026-07-12).** L1 flashed on hardware; L2–L8 read as
  theory and mapped below. **Next: the port itself** — DK overlay, build, flash, PING/PONG from the IDE.

## Lesson 2 — Devicetree + GPIO (theory digest)

Hardware is described in a **devicetree** (DTS): nodes + properties, node **labels** (`&led0`),
**aliases** (`led0`) for portable app code, **bindings** (YAML, matched via `compatible`) that
validate nodes at build time. App code gets a node id with `DT_ALIAS()`/`DT_NODELABEL()`, then a
ready-to-use pin spec with `GPIO_DT_SPEC_GET()` (device pointer + pin + flags in one struct);
check with `gpio_is_ready_dt()`, configure with `gpio_pin_configure_dt()`, read/write with
`gpio_pin_get_dt()`/`gpio_pin_set_dt()`. `DEVICE_DT_GET()` fails at build time (vs the deprecated
runtime `device_get_binding()`). **pinctrl** nodes own peripheral pin muxing (default + sleep
states). Buttons declare electrical reality in the DT: `(GPIO_PULL_UP | GPIO_ACTIVE_LOW)` — the
API then returns logical values (pressed = 1).

### How this maps to textochip

- The DK overlay will declare OUR components as aliases/nodes; `hal_zephyr` reads them via
  `gpio_dt_spec` — the firmware twin of the IDE's `lib/boards.ts` logical pins.
- **pinctrl ownership** is the general form of the ESP32 "buzzer pad theft" bug we fixed in the
  compiler prologue: a pad claimed by a PWM pinctrl group must never be re-configured as plain GPIO.
- **nRF54 GPIOs are port+pin** (P0/P1/P2), not flat numbers like ESP32 — another reason the HAL
  should hold `gpio_dt_spec`s from the overlay, not raw ints.
- **Open decision for the port — button polarity ownership:** today the BROWSER compiler emits
  `READ ; NOT` because the board profile says `button_active_low`; Zephyr's DT flags would
  normalize polarity in `gpio_pin_get_dt()` instead. The two must not both invert. Options:
  (a) DK HAL returns the RAW level (matches the ESP32 contract, keep the compiler's NOT), or
  (b) declare polarity per-board in the DT and drop `button_active_low` from the profile.
  Decide when writing the overlay; (a) is the no-frontend-change default.
- **Polling, not interrupts:** the VM samples `READ` per tick (stays responsive to
  OVERRIDE/STOP); GPIO interrupts (L2 ex. 2) are noted for the low-power story, not the port.
- Blinky = `SET`+`WAIT` at C level, BUT it blocks in `k_msleep()` — our VM never blocks:
  `hal::millis` = `k_uptime_get()`, WAIT = resume-timestamp + yield.

## Lesson 3 — App elements: overlays, CMake, Kconfig, sysbuild, TF-M (theory digest)

An app = `CMakeLists.txt` + `Kconfig` (optional custom symbols) + `prj.conf` + per-board
`.overlay` + `src/`. **Overlays** patch the board devicetree per-application; the build system
auto-picks `boards/<board_target>.overlay` (underscored board target, e.g.
`nrf54lm20dk_nrf54lm20a_cpuapp.overlay` — NOT the board name). `target_sources_ifdef(CONFIG_X …)`
gates source files on Kconfig symbols. **Sysbuild** (default since NCS 2.8) manages multi-image
builds (bootloaders, netcore images). **TF-M**: build `<target>/ns` for secure/non-secure
separation, or the plain target for a single full-privilege image. Console UART on the
nRF54L15/LM20 is **`&uart20`** (`current-speed` property; DK default 115200 via the on-board
debugger's VCOM ports).

### How this maps to textochip

- **Port structure decided:** move the ESP32-specific `zephyr/app.overlay` content to
  `zephyr/boards/esp32s3_devkitc_esp32s3_procpu.overlay` and add
  `zephyr/boards/nrf54lm20dk_nrf54lm20a_cpuapp.overlay` — one app, per-board hardware files,
  auto-selected. The firmware twin of the IDE board selector.
- **Serial transport on the DK:** `&uart20` at 115200 through the debugger VCOM (J-Link CDC has
  no reset-on-DTR, unlike the ESP32 CH343 — the whole USB-CDC-ACM workaround may be unnecessary
  on Nordic). Verify which VCOM carries uart20 at bring-up.
- **TF-M: NO** — build the plain `nrf54lm20dk/nrf54lm20a/cpuapp` target (no `/ns`); secure
  separation costs flash/RAM and buys nothing for a maker runtime.
- **Sysbuild: ignore for now** (single-core app, transparent); becomes relevant with MCUboot/OTA.
- **Edge-AI gating:** wrap the TFLM/voice service in our own Kconfig symbol
  (e.g. `CONFIG_TEXTOCHIP_AI`) with `target_sources_ifdef` — base runtime stays small.
- Lesson 4 (printk/logger) is light for us: the protocol replies are plain console lines.
  PWM (TONE/SERVO), SAADC (AREAD) and NVS (SAVE) live in the *Intermediate* course — inject that
  theory ad hoc while writing the overlay instead of taking the whole second course.

## Lesson 4 — printk + the logger module (theory digest)

`printk()` is printf-lite (subset of specifiers, no float by default) and **synchronous/blocking**:
it does not return until every byte is on the wire — simple, ordered, no buffering. The **logger
module** (`CONFIG_LOG=y`, `LOG_MODULE_REGISTER(name, level)`, `LOG_INF/DBG/WRN/ERR`,
`LOG_HEXDUMP_*`) is **deferred**: a low-priority thread drains queued messages to the backend, each
prefixed with `[uptime] <level> module:`. Backends include UART and **RTT** (SEGGER J-Link
bidirectional channel — console over the debugger, no UART needed). Compile-time + run-time
filtering per module/severity. ISR rule of thumb: never do heavy work in a callback — defer to a
thread.

### How this maps to textochip

- **The IDE protocol MUST stay on plain printk/console — NOT the logger.** The IDE parses raw
  `OK:`/`ERROR:`/`PONG` lines; the logger's `[00:00:06.900] <inf> module:` prefixes would break the
  framing, and deferred output could reorder replies relative to protocol state. Synchronous +
  unprefixed is a feature here, and our replies are short (blocking cost ≈ nil).
- **RTT is the bench-debug win:** on the DK we get a SECOND channel through J-Link for debug logs,
  leaving `&uart20` clean for the IDE protocol. (On the ESP32 we had to share one serial.) If we
  ever enable `CONFIG_LOG` for debugging, point it at the RTT backend only.
- The "don't do heavy work in ISRs" rule is moot for us by design: the VM is a cooperative poll
  loop, no ISRs in the runtime path.

## Lesson 5 — UART (theory digest)

UART = P2P serial (TX↔RX crossed, agreed baud, 8n1 typical; optional RTS/CTS hardware flow
control; every Nordic DK has an onboard USB-to-UART via the interface MCU). Zephyr offers THREE
UART APIs: **polling** (`uart_poll_in` non-blocking / `uart_poll_out` blocking), **interrupt-
driven**, and **asynchronous** (`CONFIG_UART_ASYNC_API`, EasyDMA): `uart_callback_set` +
`uart_rx_enable(buf, size, timeout_us)` → `UART_RX_RDY` events carry `buf/offset/len`; on
`UART_RX_DISABLED` you must re-enable to keep receiving; `uart_tx(..., SYS_FOREVER_US)` returns
immediately. On nRF54 the instance is two-digit: **`&uart20`**. The course exercise (chars '1'-'3'
toggle LEDs over UART) is a toy version of our OVERRIDE protocol.

### How this maps to textochip

- **Best news of the lesson — the serial HAL may port with ZERO changes.** `hal_zephyr.cpp` reads
  the IDE link with `uart_poll_in()` on `DEVICE_DT_GET(DT_CHOSEN(zephyr_console))` (line ~245).
  On the DK the board devicetree already chooses `uart20` as `zephyr,console` (115200, debugger
  VCOM) — the same line of code just resolves to the right device. The whole ESP32 CDC-ACM
  workaround (prj.conf + app.overlay console redirect) simply DOESN'T APPLY on Nordic: J-Link
  VCOM has no reset-on-DTR.
- **Polling is the right API for us** (matches the §7 design: check serial between VM ticks,
  cooperative, no ISR state). The async/EasyDMA API is the documented UPGRADE PATH if bring-up
  shows dropped bytes during `LOAD` (the only high-throughput moment — program lines streamed);
  symptoms would be truncated instruction lines / `ERROR: cannot parse`.
- No flow control (8n1 @115200), matching `lib/boards.ts` baud for the DK.
- **Course milestone reached:** L2–L5 = the minimum theory for the port. Next session: write
  `zephyr/boards/nrf54lm20dk_nrf54lm20a_cpuapp.overlay` + split the ESP32 overlay, build, flash,
  PING/PONG from the IDE.

## Lesson 6 — I2C (theory digest)

I2C/TWI: 2-wire synchronous bus (SCL clock from the controller, SDA bidirectional), multi-drop
with 7-bit (sometimes 10-bit) target addresses; 100/400/1000 kbps on Nordic controllers. Zephyr
driver: `CONFIG_I2C=y`, declare the sensor as a CHILD NODE of the bus controller in an overlay
(`&i2c0 { mysensor: mysensor@4a { compatible = "i2c-device"; reg = <0x4a>; }; }`), grab it with
`I2C_DT_SPEC_GET(DT_NODELABEL(mysensor))` (bus device + address in one struct), then
`i2c_write_dt` / `i2c_read_dt` / `i2c_reg_read_byte_dt` / `i2c_burst_read_dt` /
`i2c_write_read_dt` (write register address, read back — the classic sensor read).

### How this maps to textochip

- **NOT needed for the DK port** — the ISA has no I2C opcode and the port needs none. This is
  the future path for richer sensors (IMU for a real CONTAPASSI, BME280-class temp/humidity
  instead of the analog stand-in; we own I2C level shifters + the ELEGOO kit).
- **Architecture rule when we do add one:** I2C never enters the ISA as a raw bus opcode. The
  HAL owns the bus and the transaction (same principle as MOVE owning the L298N pins); the
  language surface stays a named read — e.g. `SENSOR("temp")` → HAL does the write/read behind
  one function. Keeps the bytecode contract lean and the sensor swappable per board.
- The overlay child-node pattern is the same devicetree skill as everything else — no new
  machinery when the day comes.

**Theory milestone: COMPLETE.** L2 (devicetree/GPIO) + L3 (overlay/Kconfig/CMake) + L4 (console)
+ L5 (UART transport) are read and mapped; L6 filed for future sensors; L7–L8 (threads/sync)
optional culture. Next step is no longer course material: write the DK overlay + split the ESP32
one, build for `nrf54lm20dk/nrf54lm20a/cpuapp`, flash, PING/PONG from the IDE.

### L6 exercise nuggets worth keeping (DK-specific)

- On the **nRF54LM20 DK** the course wires external I2C on **`&i2c21`** with pinctrl
  `TWIM_SCL = P1.11`, `TWIM_SDA = P1.12` — a ready-made reference for the day we hang an I2C
  sensor off the DK headers.
- **Board Configurator** (nRF Connect for Desktop app): the DK's interface MCU has a "board
  controller" that sets **VDD (nPM VOUT1)** — e.g. select 3.3 V and *Write config* before powering
  external boards from VDDIO. Remember this at bring-up when we wire our external LEDs / buzzer /
  sensors to the headers: wrong VDD = silent components, not an error message.
- `CONFIG_CBPRINTF_FP_SUPPORT=y` enables float format in printk (+~1 KB) — we don't need it (the
  protocol prints integers), noted only to recognize the symbol.

## Lesson 7 — Threads, workqueues, the scheduler (theory digest)

Bare-metal = one sequential loop + ISRs; an RTOS adds **threads** (smallest schedulable unit),
each running/runnable/non-runnable. Zephyr spawns two **system threads**: `main` (runs your
`main()`, priority 0) and `idle` (priority 15, activates power management on Nordic when nothing
is runnable). Priorities: negative = cooperative (runs until it yields), non-negative =
preemptible (higher/equal priority ready thread can replace it; lower number wins).
**Workqueues**: work items (plain functions) processed FIFO by a dedicated thread — the standard
way to offload non-urgent work from ISRs/high-priority threads without allocating a stack per
task. Zephyr is **tickless**: the scheduler wakes on *rescheduling points* (yield, unblock,
data arrival, time-slice expiry), not on a periodic tick. ISRs preempt everything and must stay
short.

### How this maps to textochip

- **Our VM is deliberately "bare-metal style inside the main thread":** one loop = poll serial +
  `vm.tick()`. The fun symmetry: the VM is itself a tiny cooperative scheduler for BASIC
  programs — `WAIT` is our yield, `WAITING`/`RUNNING` are our thread states, resume-at
  timestamps are our rescheduling points. Same concepts, one level up. No change for the port.
- **The ONE place threads matter for us: the edge-AI service.** The planned/spec'd voice
  pipeline (mic capture → features.c → ai_infer → vm.setAiClass) is exactly the workqueue /
  dedicated-thread use case: inference is too slow for the protocol loop, so it runs at lower
  priority while the VM keeps ticking. When we bring voice to the DK, L7's material is the
  design manual for that piece.
- **Idle thread + tickless = the nRF54L battery story.** Today our loop never sleeps (busy
  poll), fine on USB power; a battery-powered tier would want the loop event-driven (sleep until
  serial data / next WAIT deadline) so the idle thread can power-manage. Not port work — future
  low-power work, and now we know its vocabulary.

### L7 exercise nuggets worth keeping

- `K_THREAD_DEFINE(id, stack, entry, …, priority, options, delay)` = static thread creation;
  stack sizes in powers of two. `k_yield()` → back of the Runnable queue (scheduler overhead,
  no power saving); `k_msleep()` → Non-runnable → the idle thread can power-manage. Prefer sleep.
- **Time slicing is ON by default since NCS v3.1.1** (20 ms) — equal-priority threads
  round-robin automatically.
- The workqueue offload pattern in full: `k_work_init(&item, handler)` +
  `k_work_submit_to_queue(&q, &item)` — the high-priority thread finishes in ~0 ms, the work
  runs at the workqueue's low priority. This IS the shape of our future voice service.
- `k_uptime_get()` / `k_uptime_delta()` for timing — `k_uptime_get()` is literally our
  `hal::millis` on Zephyr.

## Lesson 8 — Thread synchronization: semaphores + mutexes (theory digest)

Race conditions appear the moment two threads touch shared state. **Semaphores**
(`K_SEM_DEFINE(sem, initial, limit)`, `k_sem_take`/`k_sem_give`): counted resource signaling, no
ownership — give is legal from an ISR (the "wake a thread from a driver callback" pattern).
**Mutexes** (`K_MUTEX_DEFINE`, `k_mutex_lock/unlock`): binary, OWNED (only the locker unlocks),
priority inheritance, never in ISRs. Copy shared values inside the critical section before
printing/using them outside it.

### How this maps to textochip

- **The runtime needs NONE of this today — by construction.** One loop, one thread, zero shared
  state: the single-loop VM design means no races exist. This lesson is why that design is
  cheap to reason about.
- **The one future consumer: the voice service on the DK.** When mic capture + inference get
  their own low-priority thread (the L7 workqueue pattern), the handoff to the VM is
  `setAiClass(index)` — a single aligned 32-bit word, so an atomic/volatile is enough (no mutex);
  and if we ever go event-driven on serial (battery tier), the UART async callback giving a
  semaphore to wake the protocol loop is the textbook ISR→thread pattern from this lesson.

**Fundamentals course: DONE (theory).** Everything actionable is consolidated in the digests
above; the next Nordic work item is the port (overlay split + DK overlay + build + flash +
PING/PONG), tracked in the "Course progress" section.

## Port phase 1 — BUILDS GREEN for the DK (2026-07-12)

The runtime now builds for BOTH chips from one app tree:

- `zephyr/boards/esp32s3_devkitc_esp32s3_procpu.{overlay,conf}` — the former
  app.overlay + the ESP32-specific config (USB CDC-ACM console, PWM/ADC/I2S/DMA).
  ESP32 build re-verified green after the split.
- `zephyr/boards/nrf54lm20dk_nrf54lm20a_cpuapp.{overlay,conf}` — phase 1 needs NO
  devicetree overrides (console = &uart20 via debugger VCOM, storage_partition for
  NVS already in the board tree); conf sets `CONFIG_TEXTOCHIP_AI=n` (no mic wired).
- `zephyr/Kconfig` gains **CONFIG_TEXTOCHIP_AI** (default y): gates the AI service
  sources + TFLite Micro in CMake — the DK phase-1 image skips TFLM entirely.
- `hal_zephyr.cpp` gains **map_pin()**: bytecode pins are LOGICAL (shared board
  profile); the DK maps them to port*32+pin — phase 1 sends the three LED pins to
  the ON-BOARD LEDs (red→P1.22/LED0, yellow→P1.25/LED1, green→P1.27/LED2) and
  button A to Button 0 (P1.26), so the semaforo runs with zero wiring. gpio_port()
  now handles P2/P3 too. Mirrored in the product's NORDIC_PINS + docs/hardware.md.

Build (CLI, NCS v3.4.0 managed install):

```
# env from ~/ncs/toolchains/fbf7391cab/environment.json (PATH/PYTHONHOME/etc.)
export ZEPHYR_BASE=~/ncs/v3.4.0/zephyr ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
       ZEPHYR_SDK_INSTALL_DIR=~/ncs/toolchains/fbf7391cab/opt/zephyr-sdk
cd ~/ncs/v3.4.0 && west build -b nrf54lm20dk/nrf54lm20a/cpuapp \
  -d <builddir> ~/projects/textochip-runtime/zephyr -p always
```

Result: FLASH 84 KB (4%), RAM 57 KB (11%). Or simply open `zephyr/` as an
application in nRF Connect for VS Code and add a build configuration for
`nrf54lm20dk/nrf54lm20a/cpuapp` (the course flow).

**Bench next:** flash the DK, open the nRF Terminal on the VCOM @115200 → expect
`READY`; type `PING` → `PONG`; then LOAD/RUN the semaforo → the three on-board
LEDs cycle; Button 0 answers `BUTTON("A")`. Then flip the IDE's DK entry to Web
Serial connect. Phase 2: PWM (TONE/SERVO/MOVE) + SAADC + I2S mic in the DK overlay.

**Hosted DK firmware (2026-07-12).** The product serves the phase-1 DK build at
`textochip.com/firmware/textochip-nrf54lm20dk.hex`; the IDE's flash modal writes it onto the
DK's JLINK USB drive via the File System Access API (one click). After any DK rebuild worth
shipping, copy `zephyr.hex` from the build dir to the product repo's
`public/firmware/textochip-nrf54lm20dk.hex` and push. True in-browser flashing
(MCUboot + mcumgr over Web Serial) remains the roadmap.

## Bring-up VERIFIED on the real DK (2026-07-12, evening)

The runtime runs on the physical nRF54LM20 DK: flashed over J-Link (CLI:
`JLinkExe -device nRF54LM20A_M33` loadfile, or `west flash --runner jlink` —
the default nrfutil runner needs `nrfutil install device` first), then
**PING → PONG over the J-Link VCOM** and a LOAD/RUN cycling the three on-board
LEDs. Bench facts learned:

- **The console is the SECOND VCOM** (`/dev/ttyACM1`; ttyACM0 is the other UART).
- **This DK exposes NO JLINK mass-storage drive** (USB `1366:1068` is CDC-only —
  verified with lsblk): hex drag-and-drop does NOT exist here. Browser-side
  flashing therefore needs the MCUboot + mcumgr (SMP over Web Serial) route —
  now the top roadmap item; until then the DK is flashed via J-Link/VS Code.
- **nRF UARTE in poll mode DROPS bytes in line bursts** (LOAD counted 2-3 of 6
  instructions): no usable RX FIFO. Fixed with interrupt-driven RX + a ring
  buffer in hal_zephyr (`CONFIG_UART_INTERRUPT_DRIVEN=y` in the DK conf; the
  ESP32 keeps plain polling — its UART/CDC has a real FIFO). After the fix a
  13-instruction LOAD counts 13/13.
- Junk bytes can sit in the line buffer at port open — senders should write a
  bare `\n` before the first command (the IDE backend now does).

**probe-rs — the one-command flasher (2026-07-12, bench-verified).** Saverio pushed on "how does
a NEW user get the firmware with the least friction" and the re-canvass found what the first
pass under-weighted: **probe-rs** (MIT/Apache) implements the J-Link USB protocol NATIVELY —
no SEGGER software, no nRF Connect for Desktop — and its target DB includes **nRF54LM20A**.
Verified on the real DK: probe detected (`1366:1068`), `probe-rs download --chip nRF54LM20A
--binary-format hex <file>` flashes in ~6.4 s, reset, PONG. The product now serves
`textochip.com/install-nordic.sh` (installs probe-rs if missing → fetches the hosted hex →
flash + reset), surfaced in the IDE's flash modal as the primary Linux/macOS route (Windows
keeps the Programmer app). First-flash UX ladder: one command today → MCUboot browser updates
next → pre-flashed kits at scale.

**WebUSB → J-Link: transport PROVEN in the browser (2026-07-12, bench-verified).** A minimal
local test page (navigator.usb.requestDevice → open → claimInterface on the vendor interface)
succeeded against the DK's on-board J-Link in Chrome on Linux: the browser can claim and talk
to the debugger's protocol channel directly, no drivers, no SEGGER software. Interfaces seen:
2×CDC pairs + vendor (class 255, iface 4) + HID. Caveats: the OB advertises NO WebUSB/MS-OS
BOS descriptors, so plain Windows cannot auto-bind WinUSB (Linux/macOS/ChromeOS/Android fine).
Implication: a one-click FIRST-flash on textochip.com/flash is buildable — the missing layer is
the J-Link protocol + SWD + nRF54 RRAM flash algorithm in the browser, exactly what upstream
probe-rs is porting to WASM (async branch; inspect.probe.rs). Strategy: track probe-rs-wasm
maturity and integrate rather than hand-rolling a JS J-Link driver; MCUboot remains the
update-path multiplier (one click everywhere incl. Windows, plus BLE phone updates).

**The flashing story, consolidated (2026-07-12 late).** Full rationale lives in the product
repo's ADR (docs/decisions.md "textochip-webflash"); the facts that matter here:
- ESP32 = serial bootloader in every chip's ROM (public protocol) → browser flash trivial.
  Nordic = chips deaf by design (industrial/medical security model); entry via SWD through the
  DK's SEGGER J-Link OB, whose protocol is closed and whose nRF54 build ships without SEGGER's
  own (shelved) WebUSB/MSD flashing module.
- probe-rs = the community's legal clean-room reimplementation; flashes our DK in ~6 s
  (`probe-rs download --chip nRF54LM20A --binary-format hex <hex>`); served to users as
  textochip.com/install-nordic.sh. Its WASM+WebUSB port (upstream effort branch) is the base
  for a future browser first-flash — transport already bench-verified via WebUSB claim.
- Roadmap order: port phase 2 → MCUboot+mcumgr → textochip-webflash (Saverio may develop that
  repo himself).

## Port phase 2 — design notes (PWM on the nRF54)

One structural difference from the ESP32 discovered while planning: the ESP32 LEDC gives each
channel its own timer, so ONE `pwm0` node served tone (variable Hz, ch0), servo (50 Hz, ch1)
and motors (~1 kHz, ch2/3). The nRF54 PWM peripheral has ONE period (countertop) PER INSTANCE —
channels of an instance share the frequency. So the DK mapping must SPLIT by instance:
buzzer → pwm20 ch0 (variable), servo → pwm21 ch0 (fixed 50 Hz), motors → pwm22 ch0/ch1
(~1 kHz). The HAL's single `DT_NODELABEL(pwm0)` assumption holds only for the buzzer slice;
servo/motors need per-function pwm_dt_specs (small HAL rework, board-gated). Phase 2 slice 1 =
buzzer only (overlay labels pwm20 as pwm0, pinctrl on P1.13/D5) — TONE/PLAY work with the HAL
unchanged; slice 2 = servo+motors with the per-instance HAL; slice 3 = SAADC (AREAD, P0.03);
slice 4 = I2S mic (VOICE()).

## Phase 2 slice 2 (motors) — RESOLVED, on pwm21 (2026-07-12)

The HAL motor refactor is in (per-instance PWM: `tc-pwm-motor` alias → pwm22 on
Nordic, ledc ch2/3 on ESP32; `HAS_MOTORS` gates it; `move()` no-ops without the
alias). But **enabling `&pwm22` USAGE-FAULTs the nRF54LM20 at boot** — verified:
- pwm20 (buzzer, slice 1) boots and PONGs fine; adding pwm22 crashes.
- The fault is `Attempt to execute undefined instruction`, pc in
  `nrfx_power_clock_irq_handler` (nrfx_clock.c) — a clock/power path, NOT the
  pin. Reducing pwm22 to a single channel on a known-good pin (P1.07) still
  crashes, so it's the INSTANCE, not the P1.08 pin choice.
- Full-erase reflash does not help (it's a real init fault, not a stale image).

So slice 2 is **deferred**: pwm22 removed from the shipped DK overlay (the board
boots clean, motors are a no-op). To resume, debug the pwm22 clock/domain setup
on the nRF54LM20 — likely a missing Kconfig (`CONFIG_NRFX_PWM22`?) or a
clock-request path that the PWM driver triggers differently than pwm20. A
focused session with the nRF54L PWM/clock docs. The wiring guide + ESP32 motors
are unaffected (ESP32 LEDC works).

**probe-rs installer validated:** `probe-rs download … && probe-rs reset` leaves
the DK RUNNING (PONG confirmed) — the earlier "empty capture" was a host-side
reader artifact, not an installer problem.


**ROOT CAUSE + FIX (2026-07-12, bench-verified).** Two distinct nRF54L PWM
constraints, each isolated on hardware:
1. **pwm22 boot-faults the app core.** Enabling &pwm22 → USAGE FAULT at boot
   (undefined instruction, clock/onoff path). pwm20 (buzzer) and pwm21 are
   fine. pwm21 is the board's designated `nordic_expansion_pwm` — the
   app-core PWM for the header. **Motors moved to pwm21.**
2. **One PWM instance cannot span P1 AND P3.** pwm21 ch0→P1.07 + ch1→P3.06
   MPU-faulted at boot; both channels on P1 (ch0→P1.07, ch1→P1.06) boots clean.
   **Keep both motor enables on the same GPIO port.**

Final DK motor map (verified: boot + `OVERRIDE MOVE` runs, no fault; wheel spin
pending the chassis): left ENA→P1.07 (D12, pwm21 ch0), right ENB→P1.06 (D13,
pwm21 ch1); dir pins GPIO left IN1/IN2→P3.02/03 (D10/D11), right IN3/IN4→P3.05/
P1.05 (D0/D14). tc-pwm-motor alias→pwm21; HAS_MOTORS gates it. NORDIC_PINS +
map_pin synced. Lesson for SERVO (slice, later): also pwm21-family, same-port,
NOT pwm22.
