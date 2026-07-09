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

<!-- Append the next lesson's notes below this line, same shape:
## Lesson N — <title>
### … (the material)
### How this maps to textochip
-->
