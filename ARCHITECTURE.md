# Architecture & decisions

Why this runtime is shaped the way it is. The "what" is in [`README.md`](README.md); the wire
contract is in [`SPEC.md`](SPEC.md). This file is the **why** — the decisions, with the
alternatives we weighed.

## 1. A bytecode VM, not a tokenizer on the device

The browser **compiles** BASIC to a flat bytecode list **once**; the MCU only **executes** it.

- **Performance / memory.** Parsing is the expensive part — do it on the PC, not on every run on
  the microcontroller. A dispatcher over 32 opcodes is small and hard to get wrong; an extended
  text tokenizer on a system with no memory protection is a bug source.
- **Decoupling.** The grammar evolves in the browser without reflashing; the firmware evolves
  without touching the language. The **ISA + serial protocol** ([`SPEC.md`](SPEC.md)) is the only
  thing both sides must agree on, and it is deliberately small and stable.
- **Simulator almost free.** The browser ships a TS VM with the *same* ISA, so the same bytecode
  runs in an in-browser simulator with identical semantics — no hardware needed to build or demo.

## 2. The HAL is the only per-board file

The VM / ISA / missions are platform-agnostic and only ever call `hal::*` (`src/hal.h`, **30
functions**, of which **22 are the port** and 8 are bench probes): GPIO + ADC, an ultrasonic
distance read, a buzzer tone, a servo, differential-drive motors, an I2S mic + camera capture
(grayscale + RGB) for the edge-AI tier, flash persistence for autorun, a millisecond clock, and a
serial link. The eight probes (`micStatus`, `micPinsProbe`, `aiCaptureRaw`, `camProbe`, `camPinsProbe`,
`camBitbangProbe`, `railProbe`, `padScan`) exist because bring-up questions must be answerable
from the board itself — each eliminates exactly one hypothesis, which is what turned a mute camera
into "this pad is shorted to ground" instead of "check your six wires". A new port may stub them. Porting to a new microcontroller = implement **one file**. `host/hal_host.cpp` runs the whole thing on a PC (g++)
for fast verification; `zephyr/src/hal_zephyr.cpp` is the on-board implementation. This is what
makes "runs on any microcontroller" true rather than aspirational.

## 3. Two kinds of mission — and why most are bytecode

> **Product direction (2026-07): composable `MISSION` is legacy.** In the product a "mission" is now
> a **standalone reactive Chip BASIC program** (typically an always-listening poll loop over
> `READ`/`AREAD`/`INFER` driving `SET`/`TONE`/`SERVO`/`MOVE`), not a block that calls another
> mission. The compiler emits **no new `CALL`**. The `CALL` opcode + the native mission registry
> (`registry.cpp`, `pianola.h`) are **retained in this firmware** so previously-saved bytecode that
> contains `CALL` still runs; the two-kinds design below is kept for that backward compatibility.

A "mission" is a complete, one-line behaviour (`MISSION "SEMAFORO" WITH …`). Under the hood it is
implemented one of two ways:

- **Bytecode-expandable** — the browser compiler *expands* the mission into the existing ISA
  (loops, `WAIT`, `READ`, `TONE`, the button poll). It runs on the one VM both sides already have,
  in the **browser too**. No code in this repo, and crucially **no "twin"** to keep in sync.
- **Native C++** — a richer behaviour kept here as a `MISSION` library, dispatched by `CALL`
  (e.g. `pianola.h`). The simulator runs a small TS re-implementation; hardware-only missions
  (sensors / NPU / motors) are simply "board only" in the simulator.

**Why prefer bytecode.** The missions that *need* simulating in a browser are the simple
LED/buzzer/button ones (the kid-facing tier) — and those express cleanly as bytecode. The
genuinely rich missions use hardware a browser does not have, so they can't be simulated anyway.
Expressing simple missions as bytecode kills the "implement it twice (C++ + TS twin)" problem at
the source, with zero new toolchain and **guaranteed parity** (it is literally the same bytecode
on both sides).

### WASM was considered, and not taken

We de-risked compiling this C++ core to **WebAssembly** (so the *exact* firmware would run in the
browser via a JS HAL). It works and would give perfect parity for arbitrary C++ — but it adds a
toolchain (Emscripten), a binary artifact, and harder debugging, and it does **not** help the
hardware-rich missions (no sensors in a browser regardless). For this product the **bytecode-
template** path is lighter and more in spirit. WASM stays a documented option for a future
"rich *and* browser-simulatable" mission that doesn't fit the ISA.

## 4. Missions are finite

A mission runs **once** — a beginning and an end — then control falls through to the next line.
*A robot is a sequence of missions.* Repeat with `GOTO` (`10 MISSION "SEMAFORO"` / `20 GOTO 10`),
compose them in sequence, branch with `IF`. (Earlier the traffic light looped forever internally;
making missions finite is what lets them be sequenced.)

## 5. The manifest is the single source of truth

Each mission's **tweakable parameters + guardrails** (ranges, allowed values, invariants) are
declared once, in the product's mission *manifest*. The compiler validates/clamps and bakes them
in; the simulator reads them; a native mission re-clamps them (defense in depth). The firmware
stays generic — the `CALL` carries the resolved params. (Future: generate the C++ clamp tables
from the same manifest so the ranges live in exactly one place.)

## 6. The VM declares its idle — the battery hook

A scheduled program (`WAIT 6h` between pump runs) spends its life in `VM_WAITING`, and the main
loop used to spin through it at ~1 kHz. The VM now says how long it will not need the CPU
(`VM::idleForMs`), `runtime::idleMs()` gates that behind everything that DOES need per-tick
polling (a LOAD in progress, the mic/camera inference services), and the Zephyr loop sleeps on
the answer in ≤ 50 ms chunks (`IDLE_CHUNK_MS`) so a `STOP`/`OVERRIDE` arriving mid-sleep still
feels immediate. Host-tested (`make -C host test-idle`), no ISA or protocol change.

Deliberately NOT here yet, in order: **measured** light-sleep residencies (`CONFIG_PM` is bench
work — enabling it blind is how a working serial console dies); true deep sleep for hours-long
waits, which resets the chip and so needs a resume story (a rebooted autorun restarts at pc = 0 —
correct for a loop-shaped schedule, wrong in general); and any battery-life claim in the IDE,
which waits for a bench current measurement, not an estimate. The product-side ADR (its
`docs/decisions.md`, 2026-08-29) carries the staged plan.

## 7. Open core

This repo (the **runtime + reference missions + the ISA/protocol contract**) is the open part,
Apache-2.0 when public. The commercial product is the browser **IDE + compiler + simulator**, the
**AI assistant** ([textochip-api](https://github.com/mazzasaverio/textochip-api)), the **curated
mission catalog** (which missions ship, the manifest), and the **hardware kits**. Open-core: an
open runtime others can audit, port, and contribute missions to, without giving away the IDE/AI
moat. Edit the firmware **only here** — the product no longer carries a copy.

## Roadmap

- Done (2026-07-13): the **Nordic nRF54LM20 DK** port — serial protocol, on-board
  LEDs/button, buzzer PWM, and `MOVE` driving real wheels, all bench-verified (the
  per-board work was indeed just the HAL pin map + `zephyr/boards/` files). Remaining
  on the DK: the I2S mic (voice) and a SAVE bench check.
- **Edge-AI on the nRF54L** (voice keyword on the M33 via CMSIS-NN; the Axon NPU as an
  optional accelerator) — the long-term differentiator.

Done: **flash persistence + boot autorun** (`SAVE`/`CLEAR`, run-on-boot) for
PC-unplugged autonomy — NVS on the board's `storage` partition (see `runtime.cpp`
+ `hal_zephyr.cpp`; `SPEC.md` documents the protocol).
