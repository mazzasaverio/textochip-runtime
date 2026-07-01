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

- [ ] **Freenove ESP32-S3** (WROOM, N8R8) — it has **two USB-C ports** ("USB UART" + "USB OTG")
- [ ] **INMP441** I2S digital microphone
- [ ] **Robot chassis** + **L298N** motor driver + a **motor battery pack** (e.g. 2S Li-ion / 6×AA)
- [ ] **2× USB-C cables** (one is fine if you re-plug; two is smoother — one per port)
- [ ] Jumper wires + a small breadboard
- [ ] (optional) breadboard LEDs / passive buzzer for the other missions

---

## 1. Wiring

> ⚠️ The mic + motor pins are **PROVISIONAL** — the values baked into the firmware today. If one
> clashes with your chassis, change it in the source and rebuild (see §5), don't rewire around it.

### Microphone — INMP441 → ESP32-S3  (I2S; pins from `zephyr/app.overlay`)

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
- [ ] **Nothing moves** → check the **motor battery**, the **common ground**, and that ENA/ENB are on GPIO12/21.

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
| No `PONG` on connect | Connect-time reset dropped it → **RST**, wait 3 s, retry. |
| IDE won't open the port | Connected to the **"USB UART"** port by mistake — use **"USB OTG"**; check `dialout`. |
| Robot doesn't move | Motor battery flat? **Common ground** missing? ENA/ENB not on GPIO12/21? |
| Wheel spins backwards | Swap that motor's **IN1/IN2** (or negate the speed in the program). |
| Buzzer/servo silent | A `MODE` stole the PWM pad → press **RST** (boot re-applies the pinctrl). |
| `VOICE()` always "none" | The model is in the firmware — check the I2S mic wiring (BCK/WS/SD, L/R→GND) and that you're speaking a **trained** word (go/left/right/stop). |
| Robot reacts to **background noise** | The confidence gate is too low: raise `kMinConfidence` in `src/ai/ai_service.cpp` (default 0.6) and rebuild. Too twitchy → ↑, misses clear words → ↓. |
| `west flash` fails | Hold **BOOT**, tap **RST**, release **BOOT**, retry; flash via the **"USB UART"** port. |

---

## 5. Changing a provisional pin

- **Mic (I2S):** edit the pinmux in `zephyr/app.overlay` (`i2s0_default`: `I2S0_I_BCK_GPIOxx`,
  `I2S0_I_WS_GPIOxx`, `I2S0_I_SD_GPIOxx`) → `west build` → `west flash`.
- **Motors (L298N):** edit `hal_zephyr.cpp` `move()` (the `drive_motor(IN1, IN2, pwmCh, …)` calls)
  **and** the matching LEDC channel pins in `app.overlay` (`LEDC_CH2/CH3_GPIOxx`) → rebuild.
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
