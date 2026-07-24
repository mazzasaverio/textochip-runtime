# Text to Chip runtime — bytecode ISA & serial protocol (the contract)

This is the **stable contract** between three independently-evolving parts:

- the **browser compiler** (BASIC → bytecode) — in the `textochip` product repo;
- the **in-browser simulator VM** — same repo;
- **this firmware VM** — `src/vm.cpp`, run on the board (ESP32-S3 / Nordic nRF54L
  / any board via the `src/hal.h` HAL).

As long as both sides honour this document, the compiler grammar and the firmware
can change without breaking each other. Treat changes here as **versioned and
deliberate**.

> **Contract version: 2** (Tier-1 + Tier-2 + the `TONE`/`RPIN`/`SERVO`/`AREAD`/`MOVE`
> + `MODE … INPD` real-hardware extensions + the Tier-4 edge-AI `AISTART`/`INFER`).
> `DIST` (ultrasonic distance in cm) is implemented in the firmware VM + HAL (host-tested); the
> Zephyr HC-SR04 driver ships behind a board-overlay alias — bring-up is wiring the sensor.

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
| `CALL <missionId> …` | *(legacy)* start a native MISSION; operands carry pins then `k=v` params |
| `HALT`             | stop the program                                              |
| `NOP`              | no-op                                                        |

> **`CALL` is LEGACY / firmware-retained.** The composable `MISSION "X"` block that compiled to
> `CALL <missionId>` (dispatched to a native C++ mission by `registry.cpp`) is **retired in the
> product** (2026-07): the IDE now generates **standalone reactive** Chip BASIC — a poll loop over
> `READ`/`AREAD`/`INFER` driving `SET`/`TONE`/`SERVO`/`MOVE` — and emits **no new `CALL`**. The
> opcode and the native mission registry stay in this firmware so previously-saved bytecode that
> still contains `CALL` keeps running unchanged.

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
| `MOVE <left> <right>`| differential drive: set the two wheel speeds, each `-255..255` (sign = direction, magnitude = PWM duty); `0 0` stops. Unlike the others, `MOVE` carries **no pin** — the two motors are a fixed board wiring owned by the HAL (an L298N: per wheel, 2 direction GPIOs + 1 PWM enable), so the bytecode stays board-generic. The browser sim drives a robot from these speeds. |
| `DIST`               | push the ultrasonic distance in **cm** (an HC-SR04) onto the value stack. Like `MOVE`, carries **no pin** — the HAL owns the trigger/echo. Compiled from `DISTANCE()`. **Implemented** in the VM + HAL (`hal::distanceCm`, host-tested via `make test-dist`); the Zephyr driver (10 µs trigger, echo-width timing) is wired behind the `hcsr04-trig`/`hcsr04-echo` board-overlay aliases and returns ~400 ("nothing in range") until the sensor is present — bring-up is wiring it + adding the aliases. |

#### Pin operands: what is baked vs. HAL-owned

- **Digital / analog reads carry a baked pin.** The product's higher-level reads compile to the
  plain ISA with a **concrete GPIO** resolved from the board profile — there is no `SENSOR` opcode.
  `SENSOR("obstacle")` → `READ <obstacle-pin>`, `SENSOR("motion")` → `READ <pir-pin>` (an
  `INPD` pull-down input), `SENSOR("line-left"|"line-center"|"line-right")` → `READ <line-pin>`,
  and analog sensors → `AREAD <pin>`. On the ESP32-S3 the profile currently bakes
  `obstacle=41`, `motion(pir)=7`, `line-left/center/right=38/39/40`, `analog=9`
  (`lib/boardProfile.ts`) — **provisional pins**, to finalize against the Freenove pinout.
- **`MOVE` carries no pin.** The two-motor L298N is a **fixed board wiring owned by the HAL**
  (provisionally GPIO `10/11/12/13/14/21` = leftDir1/leftDir2/leftPwm/rightDir1/rightDir2/rightPwm on
  the ESP32-S3 profile — also provisional). The bytecode only carries the two wheel speeds.
- **On Zephyr a `<pin>` operand is advisory for PWM peripherals.** For `TONE`/`SERVO` the raw GPIO
  in the bytecode names the intent, but the actual channel is **fixed in the board overlay/pinctrl**
  (`zephyr/boards/esp32s3_devkitc_esp32s3_procpu.overlay`: LEDC PWM ch0 on GPIO 5 for the buzzer,
  ch1 for the servo). Repoint the
  pinctrl group to change the physical pad. Plain-GPIO ops (`SET`/`READ`/`AREAD`/`MODE`) use the
  baked pin directly via `hal_zephyr.cpp` (`raw = port*32 + pin`).

### Tier 4 — edge-AI (contract **v2**, implemented in the VM — see [`docs/edge-ai.md`](docs/edge-ai.md))

> Additive and backward-compatible: a receiver that ignores unknown opcodes keeps running
> today's programs unchanged. A trained model
> ([`textochip-ml`](https://github.com/mazzasaverio/textochip-ml)) becomes a value the VM reads
> inline. The opcodes are **in the firmware VM** (`src/vm.cpp`), the mic capture (`hal::aiCapture`,
> I2S) + the background inference service (`src/ai/ai_service.cpp`) + on-device **TFLite Micro**
> inference all build for the ESP32-S3 (`west build` green — the model + interpreter in the ELF);
> the bench mic bring-up + on-chip validation are what remain.

| Instruction        | Meaning                                                            |
|--------------------|-------------------------------------------------------------------|
| `AISTART <model>`  | flag that the program wants `<model>` running; the board's background inference service keeps the class register fresh |
| `INFER <model>`    | push the latest class index for `<model>` (`0` = none) onto the value stack — **non-blocking**, like `AREAD` |

The compiler maps `VOICE()` → `INFER voice`; `VOICE()="stop"` resolves `"stop"` to the model's
class index (from its `labels.json`, mirrored by the product's `VOICE_LABELS`). Executing `INFER`
also starts the listening service, so `AISTART` is optional — the compiler emits none. The same int8
`.tflite` runs on the ESP32-S3 (TFLM + ESP-NN) and the Nordic nRF54L (TFLM + CMSIS-NN on the M33,
or the Axon NPU); the on-device feature extractor matches the training MFCC via a shared
golden-vector contract. **Vision is the same shape, one sense apart:** `SEE()` → `INFER vision`,
a separate `visionClass` register, and a camera vision service (`src/ai/vision_service.cpp`) with
two compile modes on one `SEE()` register: a trained OBJECT classifier (`ai_infer_vision`,
grayscale, person/ball/hand = 1..3) and the near-term COLOUR detector (`TEXTOCHIP_VISION_COLOR`:
`tc_detect_color` over an RGB frame, yellow/red/green/blue = 4..7, orange/pink = 8..9). Both feed one HAL capture
function per format (`camCapture` / `camCaptureRGB`). Full design + status:
[`docs/edge-ai.md`](docs/edge-ai.md).

**One frame, three reads.** A class alone is not actionable — a robot that must "find the ball"
needs a direction to steer in and a way to tell near from far. So the same camera frame also
fills two more registers, read through the same opcode with their own model operand:

| Instruction         | Product | Meaning                                                        |
|---------------------|---------|----------------------------------------------------------------|
| `INFER vision`      | `SEE()`     | WHAT is in view: the class index (`0` = nothing)            |
| `INFER visionx`     | `SEEX()`    | WHERE it is across the frame: `0` = far left … `100` = far right |
| `INFER visionsize`  | `SEESIZE()` | HOW BIG it looks: `0..100` = the share of the frame it covers (bigger = closer) |

All three come from ONE `vision_service::poll()` (the colour detector returns class + centroid +
coverage in a `tc_color_blob`), so they always describe the same frame. With nothing in view all
three read `0`; since size `0` unambiguously means "nothing seen", `SEESIZE() > 0` is the honest
gate before trusting `SEEX()` (`0` is also a legitimate far-left position). The OBJECT-model build
returns a class with no blob geometry, so `visionx`/`visionsize` read `0` there.

The detector's coverage threshold is a **noise floor** (a couple of percent), not a "close enough"
test: it reports a distant object and lets the program decide, because that decision belongs in
BASIC (`IF SEESIZE() >= 45 THEN MOVE 0 0`), not in the firmware.

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
| `MIC` (debug)       | sample the mic + report its level — bench aid to confirm the I2S/TDM mic is alive. The trailing `[…]` is the capture-start diagnostic (device-ready + i2s_configure/i2s_trigger return codes + start DC): `n>0` with `level=0` means the peripheral runs but no audio reaches the data pin (wiring/power); `started=0` means the driver rejected the config | `OK: mic n=N peak=P level=L [started=.. ready=.. cfg=.. trg=.. dc=..]` |
| `MICRAW` (debug)    | dump both mic channels + first raw words — tells a left/right channel-select mismatch (data on the R slot) from a dead data pin (all zero) | `OK: micraw words=N Lpeak=.. Rpeak=.. first: (L,R)…` |
| `MICPINS` (debug)   | count toggles on the mic's SCK/WS/SD pads while capturing — proves whether the bit clock physically leaves the chip (`sck tog=0` while "capturing" = the pin mux no-ops on that GPIO port; caught the nRF54L "TDM can't drive P2 pads" gotcha) | `OK: micpins pads sck=P… tog=.. ws=… sd=…` |
| `CAM` (debug)       | ask the camera who it is and how big one frame measures — the first thing to check when the Arducam goes on the bench, since everything either side of the SPI read is host-tested | `OK: cam id=0x81 fw=Y-M-D/vv frame=18432 (expect 18432)` or `OK: cam absent (…)` |
| `STORE?` (debug)    | report the autorun store: is it mounted, how many bytes are persisted, and its geometry. Diagnoses a `SAVE` that does not autorun on boot (`saved<0` = nothing stored) | `STORE mounted=0\|1 saved=N sector=WxC` |
| (boot)              | if a saved program exists, load + run it (PC-unplugged autonomy) | `READY` then `OK: autorun N` |

While a **voice program runs**, the firmware streams edge-AI feedback on the console: a heartbeat
every ~2 s (`mic: level=… env=… gain=… spk=… top=<label> NN% mfcc=…ms infer=…ms`) so a silent mic
is visible at a glance, plus a `VOICE: <word> NN%` line on each accepted detection (and
`voice? <word> NN% (…)` for one rejected by the confidence gate / lockout). These are diagnostics,
not part of the machine-parsed reply protocol.

`SAVE` persists exactly the raw bytecode just received via `LOAD` (one slot — a
new save overwrites it); on boot the firmware reloads + runs it, so the board is
autonomous with no PC attached. The main loop keeps pumping serial during autorun,
so an IDE can connect and `LOAD`/`STOP` to take over at any time. `CLEAR` returns
the board to booting idle. Storage backing is per-board (the HAL detail below the
contract): the ESP32-S3 uses **NVS** on its NOR flash; the nRF54LM20 DK uses a
length-prefixed blob written straight to the storage partition via **`flash_area`**
(its RRAM is byte-writable with no explicit erase, where NVS does not persist).

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
Porting to a new microcontroller means implementing **one file** (26 functions:
GPIO + ADC, buzzer tone, a servo, differential-drive motors, an I2S mic + camera capture
(grayscale `camCapture` for objects, `camCaptureRGB` for the near-term colour path) for the
edge-AI tier, flash persistence for autorun, a millisecond clock, and a serial link). See
`src/hal.h`.
