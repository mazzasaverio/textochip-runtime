# Decisions

Non-obvious project decisions are recorded newest first. Earlier technical
rationale remains in [ARCHITECTURE.md](../ARCHITECTURE.md); new or changed
policy belongs here.

## 2026-10-07: Port the runtime to the Arduino UNO R4 WiFi (no AI tier)

**Decision:** Add `arduino_uno_r4@wifi` as a third reference board, built from
upstream Zephyr's board definition plus our own `.conf` and `.overlay`. It runs
the VM, the serial protocol, SAVE with boot autorun, digital I/O, PWM (buzzer,
servo) and AREAD. The edge-AI tier is off: `VOICE()` and `SEE()` read "none".

**Context:** Schools already own UNO-shaped boards and recognise the Arduino
brand. The classic UNO R3 (ATmega328P, 2 KB RAM, not supported by Zephyr) cannot
host the VM; the UNO R4 is the current UNO and is supported upstream. Its RA4M1
has 32 KB RAM and 256 KB code flash, enough for the VM and too little for TFLM.

**Board facts that shape the port:**
- The USB-C port goes through an ESP32-S3 bridge wired to the RA4M1 on P109/P110
  (SCI9). Zephyr's default console is SCI2 on D0/D1, so the overlay moves the
  console to SCI9, or the IDE would not see the board over Web Serial.
- There is no `storage` partition upstream: NVS goes on the 8 KB data flash.
- Logical pins map to the Arduino `D`/`A` numbers printed on the board, through
  the upstream `arduino_header` connector.
- I/O is 5 V. Kit parts that are 3.3 V only must not be wired to it.
- The code partition starts at 0x4000, so the Arduino bootloader stays intact.
  Flashing uses pyOCD through the bridge's CMSIS-DAP interface (to verify on the
  bench).

- RAM is the binding constraint. One `Instruction` is about 90 bytes, so the
  default 256-instruction program alone needs 22.8 KB. The board sets
  `CONFIG_TEXTOCHIP_MAX_PROGRAM=128` (the longest bundled non-AI example compiles
  to 60) and `CONFIG_TEXTOCHIP_STORE_BYTES=2048`. The IDE warns at the same cap.
- `SAVE` uses the length-prefixed blob store written for the nRF54L, plus an erase
  before each write: NVS would cap a saved program at one 1 KB data flash sector.

**Out of scope for now:** motors, the 12x8 LED matrix, Wi-Fi, the UNO R4 Minima,
in-browser flashing, any AI tier.

**Verification:** `west build -b arduino_uno_r4@wifi zephyr` succeeds and its
RAM and flash usage are recorded here; the host tests still pass. On the bench:
LED, button, buzzer, AREAD, SAVE and reboot autorun over the IDE's Web Serial.

**Status 2026-10-07:** build-proven. 90.2 KB of 240 KB flash, 31.1 KB of 32 KB
RAM. The published Nordic hex was not rebuilt: this change alters the nRF54L
image only through a behaviour-neutral refactor of the store (48 bytes), and a
republished hex needs a bench check first. Host suite green; the nRF54LM20A build is unchanged in RAM. The ESP32-S3
build overflows `dram0_0_seg` by 31404 bytes on local upstream Zephyr 4.4.99 both
with and without this change, so that failure predates it. Bench pending.

## 2026-09-03: Publish the runtime as Apache-2.0 open core

**Decision:** Publish this repository, including the VM, protocol, HAL, reference
board ports, compatibility missions and bundled open-path inference artifacts,
under the Apache License 2.0. Keep the product IDE, AI service and model-training
lifecycle in their separate repositories.

**Context:** Hardware users need to audit what runs on their board, keep saved
programs usable without a service, and port the runtime to hardware we do not
own. The runtime already exposes a small stable contract and has a host test rig,
so it can support outside work without exposing the hosted product stack.

**Why Apache-2.0:** It permits maker, education and commercial use while
providing an explicit patent grant. That lowers friction for board vendors and
downstream firmware distributions.

**Rejected alternatives:** Keeping the runtime private weakens the autonomy and
portability claims. Publishing all four repositories would also expose the
hosted assistant and proprietary training workflow without improving firmware
portability. A strong copyleft licence would create more integration friction
for hardware vendors at this stage.

**Constraints:** Third-party components retain their own licences and must be
documented in `THIRD_PARTY_NOTICES.md`. Nordic's restricted Edge AI components
remain external build-time dependencies. The project name and logos are not
granted as trademarks by the source licence.

**Validation:** Before publication, the tracked tree and all 145 existing
commits were scanned for high-confidence secrets and suspicious credential
filenames, the submodule boundary was checked, and the host test suite and CI
configuration were reviewed.

**Reopen when:** A hardware partner requires a different contribution model, a
third-party component cannot be distributed within this boundary, or the open
runtime no longer produces measurable portability, trust or contributor value.
