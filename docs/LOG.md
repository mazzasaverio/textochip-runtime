# Project log

## 2026-10-16

- ESP32-S3 overlay: `hcsr04-trig` = gpio0 3, `hcsr04-echo` = gpio1 14 (GPIO46), with
  `textochip,gpio-pin` nodes. The pads are the IDE profile's RGB LED green (3) and blue
  (46); the owner chose them over PIR/blue LED and relay/PIR (2026-10-15). Both are
  strapping pins that must be low at reset; the HC-SR04's idle levels are low.
- `distanceCm()`: the `g_hc_ready` latch is gone; TRIG and ECHO are configured on every
  call (a shared pad may have been an output in the previous program).
- The IDE's `lib/boards/esp32s3.json` gets `hal.distance` {GPIO3, GPIO46} in the same
  task.

## 2026-10-10

- Aligned the repo with the owner's core standard: `AGENTS.md` rewritten to the template
  (the vendored ops kernel and `.agent-standards/` removed), root `Makefile` with
  `check`, `check-fast` and `dev` over the host suite, Claude hook running
  `make check-fast` after edits, `.agents/skills` linked to `.claude/skills`,
  `docs/research/` started, `docs/CHANGELOG.md` added at 0.1.0.
- `docs/VISION.md` and `docs/ROADMAP.md` moved to `docs/archive/`: direction now lives
  in the `textochip` repo's numbered documents.
- `make check` green (host build and the eight secret-free host tests).

## 2026-09-03

- Published the runtime repository as open source under Apache-2.0.
- Added project vision, outcome-based roadmap, decision record, contribution and
  security guidance, and explicit third-party notices.
- Audited the tracked tree and 145-commit history for common secret patterns and
  suspicious credential files; none were found.
- Verified the complete host suite, including TFLite voice and vision paths; the
  public GitHub Actions run passed and private vulnerability reporting is active.
- Updated the CI checkout action to its Node 24 release after the first public
  run exposed GitHub's Node 20 deprecation warning.
- Next: prove clean community hardware builds, then cut a versioned release.

## 2026-10-14: SENSE and the BME280 (0.6.0)

- `OP_SENSE` parses `SENSE <dev> <chan>` (lowercase letters and digits) into
  `missionId`/`callParams`; the VM pushes `hal::sense(dev, chan)`. The Zephyr HAL reads a
  node labelled `tc_bme280` (`bosch,bme280`), samples at most once a second and rounds
  (pressure: kPa × 10 = hPa). `hal::sensors()` feeds `CAPS`.
- ESP32-S3: i2c0 on GPIO38/39 with its own pinctrl group (`tc_i2c0_default`). UNO R4:
  the node goes on iic0 (Qwiic), which this overlay had disabled with the other unused
  upstream peripherals and now re-enables; the header's iic1 (A4/A5) stays disabled, so
  A4/A5 stay plain GPIO. `CONFIG_I2C` and `CONFIG_SENSOR` on both. Nordic: none.
- UNO R4 RAM: 31552 of 32768 B (96.29 %, +248 B for the I2C and BME280 drivers), the
  first CI build failed on `__device_dts_ord_101` (iic0 still disabled). Reproduced and
  fixed locally with the NCS toolchain's Zephyr SDK 1.0.1 against upstream ef440e43c.
- The PSRAM build (`TC_BIG_RAM`): `dram0_0_seg` 66.28 %, CI run 38515172514.
