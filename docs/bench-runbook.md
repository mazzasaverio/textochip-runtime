# Bench runbook — the voice-controlled robot

> **Goal of the session:** a child says **"go"** → the robot drives; **"stop"** → it halts
> (plus "left"/"right" to steer). It runs the *same* bytecode the browser simulator runs —
> compiled once in the IDE, executed by the on-board VM.
>
> **Status legend:** ✅ works after flashing today · 🎤 built into the firmware — the ESP32-S3 build
> now **links real TFLite Micro inference** (`west build` green, the model in the ELF); only the
> physical INMP441 wiring + the on-chip run remain to validate on the bench.

Follow it top to bottom. Each stage is a checkpoint — don't move on until it passes.

---

## 0. Parts checklist

- [ ] **Freenove ESP32-S3 *without* a camera** (WROOM N8R8 or **Lite**) — see the box below
- [ ] **INMP441** I2S digital microphone
- [ ] **Robot chassis** + **L298N** motor driver + a **motor battery pack** (e.g. 2S Li-ion / 6×AA;
      a 4×AA box works for bench tests — the L298N drops ~2 V, so use fresh cells)
- [ ] **USB-C DATA cable** (many USB-C cables are charge-only — if the PC sees nothing, swap the cable first)
- [ ] Jumper wires: **female-female dupont** for the pin headers + **bare-ended wires** for the screw
      terminals (a dupont female does NOT fit a screw terminal; strip ~2 cm if the bare end is short)
- [ ] (optional) breadboard LEDs / passive buzzer for the other missions

> ### ⚠️ Which board? NOT the camera one.
> On ESP32-S3 CAM boards the camera's parallel bus **owns most of GPIO 4–18** — exactly the
> motor (10/11/12/13/14) and mic (15/16/17) pins. So: **robot + voice = a camera-less board**
> (e.g. the Freenove **Lite**); the **CAM board is for the vision tier only**. Verified at the
> bench 2026-07: same firmware runs on both, only the free pins differ.

---

## 1. Wiring

> ⚠️ The mic + motor pins are **PROVISIONAL** — the values baked into the firmware today. If one
> clashes with your chassis, change it in the source and rebuild (see §5), don't rewire around it.

### Microphone — INMP441 → ESP32-S3  (I2S; pins from the ESP32 board overlay)

| INMP441 | ESP32-S3 | meaning |
|---------|----------|---------|
| VDD | **3V3** | power (NOT 5V) |
| GND | **GND** | ground |
| SCK | **GPIO15** | I2S bit clock (BCK) — ESP is master |
| WS  | **GPIO16** | word select / LR clock |
| SD  | **GPIO17** | mic data out → ESP data in |
| L/R | **GND** | ties the mic to the **LEFT** channel (the one the firmware reads) |

### Robot — L298N → ESP32-S3  (differential drive; pins from `zephyr/src/hal_zephyr.cpp`)

| L298N | ESP32-S3 | wheel |
|-------|----------|-------|
| IN1 | **GPIO10** | left — direction A |
| IN2 | **GPIO11** | left — direction B |
| ENA | **GPIO12** | left — speed (PWM) |
| IN3 | **GPIO13** | right — direction A |
| IN4 | **GPIO14** | right — direction B |
| ENB | **GPIO21** | right — speed (PWM) |
| +12V / VIN | **motor battery +** | motor supply — **not** the ESP 3V3 |
| GND | **battery −  AND  ESP GND** | ⚠️ **common ground is mandatory** |
| +5V | (leave / or → ESP 5V if your L298N needs logic 5V) | some boards jumper this |

> ### ⚠️ REMOVE the ENA/ENB jumper caps (bench lesson, 2026-07)
> L298N modules ship with black jumper caps on ENA/ENB that tie the enables to 5 V =
> **throttle always floored**. Our firmware controls speed **and stop** through PWM on those
> very pins — with the caps on, `MOVE 0 0` couldn't stop the motor (only cutting the battery
> did) and `MOVE 80` vs `MOVE 255` made no difference. **Pull both caps off** and wire
> ENA→GPIO12, ENB→GPIO21. Each EN position has **two** pins (one behind the other): use the
> **front** one, in line with the IN1…IN4 row — the pin behind it is 5 V, don't connect a GPIO
> there. (The firmware now also coasts the direction pins on speed 0, so a stop is a stop even
> on a mis-jumpered board — but you still need the EN wires for speed control.)

### Reference — the rest of the board profile (other missions, not needed for voice)

`green LED=GPIO1 · yellow=GPIO2 · red=GPIO4 · buzzer=GPIO5 · button A=GPIO6 · PIR=GPIO7 ·
servo=GPIO8 · analog=GPIO9`. Avoid strapping pins (0, 3, 45, 46), USB (19, 20), and flash/PSRAM.

---

## 2. Build & flash (the two-port flow)

The ESP32-S3 has **two** USB ports with **two** jobs:
- **"USB UART"** (CH343 chip) → **flashing** (`west flash`). Its DTR/RTS auto-reset the chip.
- **"USB OTG"** (native USB) → **the IDE** (an app CDC-ACM; no reset-on-DTR, so Web Serial opens cleanly).

**Activate your Zephyr env** (west + SDK — see the repo README; example paths):

```bash
export PATH=$HOME/.venvs/zephyr/bin:$PATH
export ZEPHYR_BASE=$HOME/zephyrproject/zephyr
export ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-1.0.1
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
```

**Build, then flash via the "USB UART" port:**

```bash
cd ~/projects/textochip-runtime
west build -b esp32s3_devkitc/esp32s3/procpu zephyr
west flash        # cable in the "USB UART" port
```

- [ ] If flash can't auto-reset into download mode: **hold BOOT**, tap **RST**, **release BOOT**, retry `west flash`.
- [ ] Then **move the cable to the "USB OTG" port** for the IDE.
- [ ] Linux: your user must be in the `dialout` group (`sudo usermod -a -G dialout $USER`, then re-login).

> **Planned — browser flashing (no toolchain):** this whole section is developer-grade; a
> teacher will never run `west`. The plan (product strategy §10) is a **`/flash` page** on
> textochip.com using [ESP Web Tools](https://esphome.github.io/esp-web-tools/) (ESPHome-proven,
> ESP32-S3 supported): one **Install** button in the browser, the manifest + `zephyr.bin` hosted
> by the site. Until that ships, `west` below is the way.

**Single-port boards (e.g. the Freenove Lite):** one USB-C does both jobs, but there's no
auto-reset chip — the BOOT/RST dance is **always** needed to flash: hold **BOOT**, tap **RST**,
release **BOOT** (the port re-enumerates as `Espressif USB JTAG/serial`, VID `303a`), then
`west flash`; esptool can find it itself, or point it: `esptool --port /dev/ttyACM0 --before
no-reset --after hard-reset write-flash … build/zephyr/zephyr.bin`. After flashing press **RST**
once — the port comes back as `Zephyr … CDC_ACM` (VID `2fe3`): that's the firmware running, and
the same port is the IDE console. **How to tell a blank board:** its USB port shows *nothing* on
the native port (no CDC console) — if the IDE gets no PONG and the OS lists no Zephyr device,
the board needs flashing, not more clicking. (On two-port boards note the CH343 "USB UART" can
enumerate as `ttyACM0` too, not just `ttyUSB0` — go by the device *name*, not the number.)

---

## 3. Bring-up — stage by stage

### Stage 1 — the board is alive ✅
1. IDE → **[textochip.com/ide](https://textochip.com/ide)** → switch to **Real board** → **Connect** →
   pick the `CDC_ACM` / native-USB port (the "USB OTG" one).
2. Expect the connection to go green. Send **PING** → **PONG**.
   - [ ] No PONG? A connect-time reset can drop the first command — press **RST**, wait ~3 s, retry.

### Stage 2 — the robot drives ✅  (this validates the L298N wiring **today**)
Use **Runtime → OVERRIDE** to poke the motors directly (no program needed):

| Type | Expect |
|------|--------|
| `OVERRIDE MOVE 160 160` | both wheels forward |
| `OVERRIDE MOVE 0 0` | stop |
| `OVERRIDE MOVE 160 -160` | spin in place |
| `OVERRIDE MOVE 40 160` | curve left |

- [ ] A wheel spins the **wrong way** → swap that motor's **IN1/IN2** wires (or negate its speed).
- [ ] **Nothing moves** → work the chain in this order (each step halves the search):
  1. **Are you talking to the real board?** The OUTPUT toggle must say **Real board**, the log
     `backend: real board`, and the header dot green. If the backend is the **Simulator**, every
     OVERRIDE happily answers `OK` — from the robot in the browser. (Classic bench trap.)
  2. **Power:** L298N red LED on? Battery switch, cell orientation, fresh cells.
  3. **Ask the board itself:** with `OVERRIDE MOVE 160 160` active, `OVERRIDE RPIN 10` /
     `OVERRIDE RPIN 11` print the live pin levels (expect `10 = 1`, `11 = 0`). Correct levels =
     the ESP32 is driving; the fault is downstream (contacts, EN jumpers, motor wires).
  4. **Wiggle test:** with the command still active, gently press each connection one at a time
     (duponts both ends, the GND link, screw terminals, motor tabs) — a twitch names the culprit.
  5. **Motors actually connected?** OUT1/OUT2 (and OUT3/OUT4) must hold the motor wires — bare
     copper clamped under the screw (not insulation), wire twisted through the motor tab eyelets.

> This is the proof that the whole `MOVE` path works on real hardware — the same opcode the voice
> program drives. Get this solid first; the voice stage only changes *what decides* the `MOVE`.

### Stage 3 — voice → robot (the hero) 🎤
The firmware **already links real TFLite Micro inference** on the ESP32-S3 (our vendored TFLM,
reference kernels — `west build` green, the model in the ELF), and the host proves the identical
code + model classify correctly (`make test-ai-service`). So the voice path is built; the bench just
validates the physical mic + the on-chip run.

With the mic wired (§1) and the firmware flashed:
1. In the IDE, load the voice program (or open the configurator's — it generates exactly this):
   ```basic
   10 IF VOICE()="go" THEN MOVE 160 160
   20 IF VOICE()="stop" THEN MOVE 0 0
   30 IF VOICE()="left" THEN MOVE 40 160
   40 IF VOICE()="right" THEN MOVE 160 40
   50 GOTO 10
   ```
2. **Upload & Run.** Say **"go"** → the robot drives; **"stop"** → it halts. (No `AISTART` needed —
   executing `INFER`/`VOICE()` starts the listening service on its own.)

**Validating the mic itself** (independent of recognition): open a plain serial terminal on the OTG
port (e.g. `screen /dev/ttyACM0 115200`, or `west build -t monitor`) and send **`MIC`** with no
program running — it replies `OK: mic n=… peak=… level=…`. **Speak:** `peak`/`level` jump from
near-zero (silence) to clearly higher; if they stay 0, the mic isn't captured — recheck the I2S
wiring (BCK/WS/SD, L/R→GND). Send `MIC` **raw over serial**, not through the IDE's `OVERRIDE` box
(which wraps it as an instruction). As a hardware fallback, scope BCK (GPIO15) / WS (GPIO16).

---

## 4. Troubleshooting (quick table)

| Symptom | Likely cause / fix |
|---------|--------------------|
| No `PONG` on connect | Connect-time reset dropped it → **RST**, wait 3 s, retry. **Still nothing + no Zephyr CDC device on the OS?** The board is blank — flash it (§2). |
| PC sees no board at all | **Charge-only USB cable** (swap for a data cable) — or you're on the port of a blank board. |
| Commands answer `OK` but nothing happens on the desk | You're talking to the **browser Simulator** — switch OUTPUT to **Real board** and Connect (check for `backend: real board` in the log). |
| IDE won't open the port | Connected to the **"USB UART"** port by mistake — use **"USB OTG"**; check `dialout`. |
| Robot doesn't move | Follow the Stage-2 chain: real backend → power LED → `RPIN` → wiggle → motor wires. |
| `MOVE 0 0` doesn't stop / speed values ignored | **ENA/ENB jumper caps still on** — remove them and wire ENA→GPIO12, ENB→GPIO21 (§1). |
| Wheel spins backwards | Swap that motor's **IN1/IN2** (or negate the speed in the program). |
| Buzzer/servo silent | A `MODE` stole the PWM pad → press **RST** (boot re-applies the pinctrl). |
| `VOICE()` always "none" | The model is in the firmware — check the I2S mic wiring (BCK/WS/SD, L/R→GND) and that you're speaking a **trained** word (go/left/right/stop). |
| Robot reacts to **background noise** | The confidence gate is too low: raise `kMinConfidence` in `src/ai/ai_service.cpp` (default 0.6) and rebuild. Too twitchy → ↑, misses clear words → ↓. |
| `west flash` fails | Hold **BOOT**, tap **RST**, release **BOOT**, retry; flash via the **"USB UART"** port. |

---

## 5. Changing a provisional pin

- **Mic (I2S):** edit the pinmux in the ESP32 board overlay (`zephyr/boards/esp32s3_devkitc_esp32s3_procpu.overlay`, `i2s0_default`: `I2S0_I_BCK_GPIOxx`,
  `I2S0_I_WS_GPIOxx`, `I2S0_I_SD_GPIOxx`) → `west build` → `west flash`.
- **Motors (L298N):** edit `hal_zephyr.cpp` `move()` (the `drive_motor(IN1, IN2, pwmCh, …)` calls)
  **and** the matching LEDC channel pins in the same overlay (`LEDC_CH2/CH3_GPIOxx`) → rebuild.
- Keep them in sync with the browser board profile (`lib/boardProfile.ts`, in the product repo) so
  the wiring reference the child sees matches the board.

---

## 6. What's proven vs. what the bench validates

| Proven (host + build, this repo) | The bench validates |
|----------------------------------|---------------------|
| The model — `go/left/right/stop`, 95.5% (`make ai-infer`) | The **physical INMP441** wiring + the 24-bit/32-bit I2S slot format |
| MFCC parity with training (`make test-ai`) | The **L298N chassis** (Stage 2, works today) |
| The service: audio → class (`make test-ai-service`) | Live **voice → robot** — the physical mic + the on-chip TFLM run |
| The voice program → `MOVE` per keyword (`make test-ai-move`) | Timing/latency of the rolling window on the real CPU |
| The whole board firmware **+ on-device TFLM** build (`west build` green) | |
