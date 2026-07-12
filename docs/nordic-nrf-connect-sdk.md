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
- **Lessons 2–3 done as THEORY ONLY** (decided: Saverio reads the theory and maps it to textochip;
  the hands-on exercises are skipped — our HAL work later IS the exercise). Course exercise repo
  cloned at `~/ncs/ncsfund` (NordicDeveloperAcademy/ncs-fund) if ever needed.
- **Next:** Lesson 4 (printk/logger — light) and **Lesson 5 (UART — the important one:** the
  IDE ↔ DK serial protocol transport). After L5: write the DK overlay and start the real port.

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
