# Text to Chip runtime — bytecode ISA & serial protocol (the contract)

This is the **stable contract** between three independently-evolving parts:

- the **browser compiler** (BASIC → bytecode) — in the `textochip` product repo;
- the **in-browser simulator VM** — same repo;
- **this firmware VM** — `src/vm.cpp`, run on the board (ESP32-S3 / Nordic nRF54L
  / any board via the `src/hal.h` HAL).

As long as both sides honour this document, the compiler grammar and the firmware
can change without breaking each other. Treat changes here as **versioned and
deliberate**.

> **Contract version: 3** (2026-10-14): contract 2 (Tier-1 + Tier-2 + the
> `TONE`/`RPIN`/`SERVO`/`AREAD`/`MOVE` + `MODE … INPD` real-hardware extensions + the
> Tier-4 edge-AI `AISTART`/`INFER`) plus the **resource table** (§1, "Contract 3"). A
> contract-3 firmware runs contract-2 programs unchanged.
> `DIST` (ultrasonic distance in cm) is implemented in the firmware VM + HAL (host-tested); the
> Zephyr HC-SR04 driver ships behind a board-overlay alias — bring-up is wiring the sensor.

---

## 1. Bytecode format

Flat and **textual**: one instruction per line, opcode then space-separated
operands, e.g. `SET 4 1`. The firmware parses each line into an instruction
struct; the simulator executes the same instructions directly.

- **Addresses** (`JMP`/`JZ`/`GOSUB` targets) are **0-based instruction indices**.
- **Pins**: in contract 3, a pin operand is `R<n>`, entry n of the program's resource
  table, which names the board's own pad ("GPIO4", "P1.22", "D4"). In contract 2 it is
  a logical pin number the firmware maps to a pad (`src/pads.cpp`, `legacyPad`).
- Two stacks: a **value stack** (ints) and a **call stack** (return addresses).
- **Variables** are integers named `a`–`z`.

### Contract 3 — the resource table

A contract-3 program opens with its resource table; the instructions then address
resources, never pins:

```
CONTRACT 3
RES 0 DOUT P1.22
RES 1 DIN P1.26 pull=up
RES 2 TONE P1.13
MODE R0 OUT
MODE R1 IN
READ R1
JZ 6
TONE R2 440
SET R0 1
```

| Line | Meaning |
|------|---------|
| `CONTRACT <n>` | the contract the program is written for; a firmware refuses a newer one |
| `RES <id> <kind> <pad> [pull=up\|down]` | resource `<id>` (numbered from 0, in order) is `<pad>` used as `<kind>` |

- **Kinds**: `DOUT` (written with `SET`, `MODE … OUT`, may be read back), `DIN` (read
  with `READ`; `pull=up`, the default, goes with `MODE … IN`, `pull=down` with
  `MODE … INPD`), `TONE` (`TONE`), `SERVO` (`SERVO`), `ADC` (`AREAD`). `RPIN` reads any.
- **Pads** are the board's own names, the ones the IDE's board descriptors print as
  `physical` (`textochip/lib/boards/<id>.json`): `GPIO<n>` on the ESP32-S3,
  `P<port>.<pin>` on the nRF54L, `D<n>`/`A<n>` on the UNO R4.
- `CONTRACT` and `RES` lines are not instructions: `JMP`/`JZ`/`GOSUB` addresses count
  instructions only. A `RES` line alone makes a program contract 3.
- **The firmware defends itself.** At `.` it checks the whole program against its
  board (`src/pads.cpp`) and refuses it, keeping nothing, with one `ERROR: <reason>`
  line instead of `OK: loaded N`:
  - a pad that is not this board's (`ERROR: R0 P1.22: no such pad on this board`);
  - a pad a program may not use: the flash, the PSRAM, the USB, a strapping pin, a pad
    shorted to ground, one a driver claims at boot (`ERROR: R0 GPIO19: the USB port`);
  - a `TONE`, `SERVO` or `ADC` resource on another pad than the one that peripheral is
    wired to (`ERROR: R0 GPIO7: this board's TONE is wired to GPIO5`). The ESP32-S3
    wires a second buzzer and a second servo (`pads::secondPad`, since 0.11.0): `TONE`
    on GPIO5 or GPIO42, `SERVO` on GPIO8 or GPIO47, each pair playing independently;
    GPIO42 and GPIO47 are plain GPIOs at boot and become PWM when a program drives them;
  - two resources on one pad, an undeclared resource, an instruction that uses a
    resource against its kind, a bare pin number in a contract-3 program.
  A saved program that fails the check does not autorun.
- **The IDE** sends contract 3 to a board whose `CAPS` says `contract=3` or more, and
  contract 2 otherwise. Loading a program (either contract) starts it with fresh
  variables and stacks, as the browser simulator does.

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
| `PIXN <count>` | set up the addressable LED strip (WS2812) for this program: `count` pixels (capped at the strip's length in the overlay, `chain-length`, 64 on the ESP32-S3), all off. Compiled once at the program's start from a `pixels` part (`REM @part striscia = ws2812 count=30`). Carries **no pin**: the strip's data pad is the overlay's (`tc_pixels`). |
| `PIX` | pop a colour (`0xRRGGBB`), then a pixel index; colour that pixel, or every pixel when the index is -1; an index out of range is ignored. The frame is sent once a tick, only when it changed (`hal::pixelsShow`). The IDE writes its colour names at a quarter of full brightness (red = `0x400000`), so 30 pixels of white draw about 0.45 A. `CAPS` lists `pixels` in `prims`. Host-tested via `make test-pixels`. |
| `DCLR` | empty every row of the screen (an SSD1306 OLED). |
| `DTEXT <row> "<text>"` | the screen's row `row` (0 = top, 4 rows of 12 characters on 128×64) becomes `text`: printable ASCII in double quotes, so its spaces stay, at most 21 characters. |
| `DNUM <row>` | pop a number, append it in decimal to the row (`DTEXT 1 "T "` then `DNUM 1` shows `T 23`). The rows are drawn once a tick, only when one changed (`hal::displayShow`), in the character framebuffer's 10×16 font. Compiled from a `display` part (`REM @part schermo = ssd1306`, `schermo(1) = "T ", clima.TEMP`). No pin: the screen is on the board's I2C bus (`tc_display`, 0x3C, the ESP32-S3 only). `CAPS` lists `display`. Host-tested via `make test-display`. |
| `SENSE <device> <channel>` | push a sensor reading, an integer in the channel's unit, onto the value stack. Carries **no pin**: the sensor sits on the board's bus (I2C) and the HAL reads it through Zephyr's sensor API, a node labelled `tc_<device>` in the board overlay. Words are lowercase letters and digits. `bme280`: `temp` (°C), `hum` (%), `pres` (hPa), sampled at most once a second. `dht22`: `temp` (°C), `hum` (%), at most once every 2 s (its driver busy-waits ~23 ms per reading). A device the board does not have, or one that fails to read, reads 0. `CAPS` lists `sense` in `prims` and the devices in `sensors=` (`sensors=bme280,dht22`). Compiled from a sensor part's channel (`clima.TEMP`). Host-tested via `make test-sense`. |

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
- **On Zephyr a `<pin>` operand is advisory for PWM peripherals** in contract 2 (contract 3
  refuses a `TONE`/`SERVO`/`ADC` resource on any other pad). For `TONE`/`SERVO` the raw GPIO
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
opcodes (forward-compatibility). A line is bounded (the firmware caps it at 512
bytes and answers `ERROR: line too long`, dropping the rest up to the next
newline) so a peer that never sends a newline cannot exhaust the device's memory;
a real command or instruction line is well under that.

---

## 2. Serial protocol

One command per line (`\n`). Replies start with `OK` / `ERROR` / `PONG` for easy
parsing.

| From the IDE        | Board action                                  | Reply                       |
|---------------------|-----------------------------------------------|-----------------------------|
| `PING`              | health check                                  | `PONG`                      |
| `CAPS`              | what this firmware is and can drive (the handshake the IDE runs on connect) | `CAPS build=<id> contract=<n> max=<n> board=<id> prims=<a,b,…>` |
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

`CAPS` (since contract 2, 2026-10-13) is one line of space-separated `key=value` pairs, so it
can grow: `build` is the runtime git hash (the same as `VER`), `contract` this SPEC's
version, `max` the program cap in instructions, `board` the id the IDE knows the board by
(`textochip/lib/boards/<id>.json`), `prims` the primitives this build compiled in, from
`digital`, `adc`, `tone`, `servo`, `move`, `dist`, `infer`, and `tone2`, `servo2` when the
board drives a second buzzer and a second servo (the ESP32-S3). The IDE ignores keys it does
not know, and a firmware that answers `ERROR: unknown command` predates the handshake.

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
