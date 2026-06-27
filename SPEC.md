# Text to Chip runtime — bytecode ISA & serial protocol (the contract)

This is the **stable contract** between three independently-evolving parts:

- the **browser compiler** (BASIC → bytecode) — in the `textochip` product repo;
- the **in-browser simulator VM** — same repo;
- **this firmware VM** — `src/vm.cpp`, run on the board (ESP32-S3 / Nordic nRF54L
  / any board via the `src/hal.h` HAL).

As long as both sides honour this document, the compiler grammar and the firmware
can change without breaking each other. Treat changes here as **versioned and
deliberate**.

> **Contract version: 1** (Tier-1 + Tier-2 + the `TONE`/`RPIN`/`SERVO` extensions).

---

## 1. Bytecode format

Flat and **textual**: one instruction per line, opcode then space-separated
operands, e.g. `SET 4 1`. The firmware parses each line into an instruction
struct; the simulator executes the same instructions directly.

- **Addresses** (`JMP`/`JZ`/`GOSUB` targets) are **0-based instruction indices**.
- **Pins** are **concrete GPIO numbers** — the compiler bakes them in from the
  board profile, so the firmware stays board-generic and stored programs run
  standalone.
- Two stacks: a **value stack** (ints) and a **call stack** (return addresses).
- **Variables** are integers named `a`–`z`.

### Tier 1 — core

| Instruction        | Meaning                                                       |
|--------------------|---------------------------------------------------------------|
| `MODE <pin> OUT`   | `pinMode(pin, OUTPUT)`                                         |
| `MODE <pin> IN`    | `pinMode(pin, INPUT_PULLUP)`                                   |
| `SET <pin> 1\|0`   | `digitalWrite(pin, HIGH\|LOW)`                                 |
| `WAIT <ms>`        | non-blocking delay (VM records a resume time and yields)      |
| `JMP <addr>`       | `pc = addr`                                                   |
| `CALL <missionId> …` | start a native MISSION; operands carry pins then `k=v` params |
| `HALT`             | stop the program                                              |
| `NOP`              | no-op                                                        |

### Tier 2 — expressions & branching

| Instruction       | Meaning                                            |
|-------------------|----------------------------------------------------|
| `PUSH <int>`      | push constant                                      |
| `LOAD <var>`      | push variable value                                |
| `STORE <var>`     | pop into variable                                  |
| `READ <pin>`      | push `digitalRead(pin)` (0/1)                       |
| `ADD`/`SUB`/`MUL`/`DIV` | pop b,a; push `a op b`                        |
| `GT`/`LT`/`EQ`    | pop b,a; push `(a op b) ? 1 : 0`                    |
| `AND`             | pop b,a; push `(a && b) ? 1 : 0`                    |
| `NOT`             | pop a; push `a ? 0 : 1`                             |
| `ABS`             | pop a; push `abs(a)`                                |
| `JZ <addr>`       | pop a; if `a == 0` then `pc = addr`                 |
| `GOSUB <addr>`    | push return address; `pc = addr`                   |
| `RET`             | pop return address; `pc = ret`                     |

### Extensions (real-hardware)

| Instruction          | Meaning                                                       |
|----------------------|---------------------------------------------------------------|
| `TONE <pin> <hz>`    | square-wave tone on a passive buzzer; `hz = 0` is off         |
| `RPIN <pin>`         | debug: the board prints `PIN n = v`                           |
| `SERVO <pin> <angle>`| position a hobby servo (e.g. SG90) to `angle` 0..180° (50 Hz PWM); the HAL maps 0..180° → 0.5..2.5 ms pulse |
| `AREAD <pin>`        | push the analog (ADC) reading of `pin` (e.g. 0..4095) onto the value stack |
| `MODE <pin> INPD`    | `pinMode(pin, INPUT_PULLDOWN)` — active-high sensors (e.g. a PIR) idle LOW when disconnected |

A receiver MUST ignore blank lines and `;` / `#` comments, and MAY ignore unknown
opcodes (forward-compatibility).

---

## 2. Serial protocol

One command per line (`\n`). Replies start with `OK` / `ERROR` / `PONG` for easy
parsing.

| From the IDE        | Board action                                  | Reply                       |
|---------------------|-----------------------------------------------|-----------------------------|
| `PING`              | health check                                  | `PONG`                      |
| `LOAD`              | begin receiving bytecode                       | `OK: send program, end '.'` |
| `<instruction>` …   | buffer instructions                            | (buffered)                  |
| `.`                 | end of program                                 | `OK: loaded N`              |
| `RUN`               | start the VM from `pc = 0`                      | streamed logs / `OK: done`  |
| `STOP`              | stop the VM                                    | `OK: stopped`               |
| `OVERRIDE <instr>`  | execute one instruction immediately            | `OK` / `ERROR`              |
| `SAVE`              | persist the loaded bytecode to flash + arm autorun | `OK: saved` / `ERROR: …`    |
| `CLEAR`             | forget the saved program (disable autorun)     | `OK: cleared`               |
| (boot)              | if a saved program exists, load + run it (PC-unplugged autonomy) | `READY` then `OK: autorun N` |

`SAVE` persists exactly the raw bytecode just received via `LOAD` (one slot — a
new save overwrites it); on boot the firmware reloads + runs it, so the board is
autonomous with no PC attached. The main loop keeps pumping serial during autorun,
so an IDE can connect and `LOAD`/`STOP` to take over at any time. `CLEAR` returns
the board to booting idle.

---

## 3. Execution model

- **Tick-based, never blocks.** The main loop calls `vm.tick()` and, between
  ticks, checks serial for control commands. `WAIT` and `CALL` are cooperative
  (resume-when-ready), so the device stays responsive to `OVERRIDE`/`STOP` while
  running.
- A program SHOULD use `JMP`/`GOTO` to loop; a mission with a beginning and an
  end falls through to the next instruction when done (a program is a sequence of
  missions).

---

## 4. The HAL (per-board surface)

Everything above is board-agnostic and only ever calls `hal::*` (`src/hal.h`).
Porting to a new microcontroller means implementing **one file** (~10 functions:
GPIO, buzzer tone, a servo, a millisecond clock, and a serial link). See `src/hal.h`.
