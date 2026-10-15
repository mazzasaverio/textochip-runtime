# Changelog

What reached the people and services that use the Text to Chip runtime, newest first,
one entry per release (`stack-rules` rule 18): semantic version, ISO 8601 timestamp
with timezone, a short name, then one bullet per change, each starting with a short
title in bold and a colon, and a last bullet on how it was verified. Its users are the
makers who install this firmware on their boards (from the `textochip` IDE or from a
terminal), the `textochip` IDE and compiler that talk to it through the bytecode and
serial contract in `SPEC.md`, and contributors who build it from source.
Implementation details stay in git and in `LOG.md`.

## 0.10.2 - 2026-10-15T22:50+02:00 - ESP32-S3: the distance sensor

- **HC-SR04 on the ESP32-S3:** `DISTANCE()` reads the sensor on TRIG GPIO3 and ECHO
  GPIO46; until now the ESP32-S3 had no hcsr04 aliases and always read 400. No pad was
  free, so the two are shared with the external RGB LED's green and blue legs: a
  program uses one or the other (the IDE says so). The board answers CAPS with `dist`.
- **The sensor's pins are set up on every read,** so a program that drove them as
  outputs before does not leave ECHO an output until a reboot (all boards).
- Verified: every host test (`make check`), CI's firmware build; build-proven, no bench
  check on an ESP32-S3 yet.

## 0.10.1 - 2026-10-15T20:00+02:00 - Nordic DK: sensors on real header pins

- **Line and obstacle sensors on P3.07–P3.10:** on the nRF54LM20 DK they were mapped to
  P2.00–P2.03, the external flash's lines, which reach header P4 only through the board
  configurator (user guide v0.7.0, Table 4); P2.00–P2.05 are now reserved.
- **The right motor's logical PWM pin maps to P1.06,** the pad the overlay drives; it
  pointed at P2.05, the flash's chip select.
- Verified: every host test (`make check`), CI's firmware build 38778527249 (77accc7),
  published without a bench check (the owner's rule of 2026-10-14).

## 0.10.0 - 2026-10-14T23:00+02:00 - TIME, and the ISA in one file

- **TIME:** pushes the milliseconds since the program started, so a stopwatch or a
  reflex game measures time instead of counting WAIT ticks; every board answers CAPS
  with the `time` primitive. Wraps after about 24 days.
- **The ISA in one file:** `src/isa.json` generates the opcode enum, the name table and
  the parser's lookup (`tools/gen_isa.py`, checked in CI); the IDE keeps a byte-identical
  copy.
- Verified: every host test, `test-time` (TIME is 0 at start, 250 after a 250 ms WAIT,
  not the board's uptime), the IDE's differential test against this VM. No bench run on
  the boards (owner rule for CI-built firmware).

## 0.9.0 - 2026-10-14T11:00+02:00 - A small screen

- **DISPLAY:** `DCLR`, `DTEXT <row> "<text>"` and `DNUM <row>` write rows of text and
  numbers on an SSD1306 OLED (128×64, four rows of twelve characters), drawn once a tick
  when a row changed. On the ESP32-S3, on the sensor bus (GPIO38/39) at 0x3C, beside
  the BME280. Not on the UNO R4 (its RAM has no room for the frame), nor the Nordic DK.
  `CAPS` lists `display`.
- Verified: a host test (a label and a reading on a row); the three boards in CI. Not on
  the bench yet.

## 0.8.0 - 2026-10-14T10:00+02:00 - An LED strip

- **PIXELS:** `PIXN <count>` sets up a WS2812 strip and `PIX` colours a pixel (or all)
  from the stack, so an index can be a variable; one frame is sent per tick, only when
  something changed. On the ESP32-S3: data on GPIO41 (SPI3's MOSI, Zephyr's
  `ws2812-spi`), up to 64 pixels. Not yet on the UNO R4 and the Nordic DK. `CAPS` lists
  `pixels`.
- **Safer boot pads:** SPI2 is disabled and SPI3 keeps only its MOSI, so the SPI driver
  never takes the motors' or the line tracker's pads.
- Verified: a host test (all red, one pixel blue, an index out of range, a chaser, one
  frame per tick); the three boards in CI. Not on the bench yet.

## 0.7.0 - 2026-10-14T09:00+02:00 - The DHT22

- **A second sensor:** `SENSE dht22 temp|hum` reads a DHT22 (AM2302) on one data line:
  GPIO40 on the ESP32-S3, A3 on the UNO R4 (both the line tracker's right pin). One
  reading every 2 s; each costs the program ~23 ms. `CAPS` lists `sensors=bme280,dht22`.
- **For contributors:** a sensor is now one row of a table in the HAL.
- Verified: the host test reads a second device (a hygrostat); the UNO R4 built locally
  (RAM 96.6 %), the three boards in CI. Not on the bench yet.

## 0.6.0 - 2026-10-14T08:00+02:00 - A climate sensor, and room on the ESP32-S3

- **Sensors through their drivers:** `SENSE <device> <channel>` pushes a reading from a
  sensor on the board's bus through Zephyr's sensor API; the first is the BME280
  (`temp` in °C, `hum` in %, `pres` in hPa), at 0x76 on the ESP32-S3's GPIO38/39 and on
  the UNO R4's Qwiic connector. `CAPS` lists `sense` and `sensors=bme280`. A missing
  sensor reads 0.
- **ESP32-S3:** the voice tier's buffers moved to the module's 8 MB PSRAM: internal RAM
  from 99.7 % to 66 %, so the firmware has room again for new primitives.
- Verified: a host test (a thermostat driven by a reading), the three boards built in CI.
  Not on the bench yet: the owner publishes CI firmware without a bench check
  (2026-10-14).

## 0.5.0 - 2026-10-14T02:30+02:00 - Contract 3: programs name the board's pads

- **Programs say which pads they use:** a contract-3 program opens with a resource table
  (`RES 0 DOUT P1.22`) and its instructions address resources (`SET R0 1`), so the IDE
  resolves pins from the board's descriptor and the firmware keeps no table of the
  IDE's numbers. `CAPS` answers `contract=3`. Contract-2 programs run unchanged.
- **The board refuses what would harm it:** a program that uses the flash, the USB, a
  strapping pin, a pad shorted to ground, the microphone's pads, or puts the buzzer,
  the servo or the analog input on a pad that peripheral is not wired to, is refused
  with the reason instead of loaded.
- **Fixed:** a new program now starts with fresh variables, as in the simulator; the
  last program's values survived a `LOAD`. On the Nordic DK the relay drives P0.04,
  where the IDE wires it, instead of the camera's SPI clock (P1.10).
- Verified: a host test of the pad rules on three boards and 9,000 generated programs
  that run the same in contract 2 and 3, the host suite, the four images built in CI.

## 0.4.1 - 2026-10-14T01:00+02:00 - The ESP32-S3 image fits again

- **The ESP32-S3 builds again:** its image had outgrown the chip's internal RAM by
  39 KB. The voice window now holds the microphone's 16-bit samples instead of floats
  (32 KB less, the same values to the bit), and a board with no camera keeps no camera
  frame (9 KB less). Nothing a program does changes on any board.
- Verified: the host tests, the voice service end to end (TTS words to MOVE) with the
  same output before and after, and the four images built in CI.

## 0.4.0 - 2026-10-13T23:00+02:00 - The board says what it is

- **The CAPS handshake:** a new `CAPS` command answers with the build, the contract
  version, the program cap, the board id and the primitives this build drives (for
  example `CAPS build=… contract=2 max=128 board=unor4wifi prims=digital,adc,tone,servo,dist`).
  The IDE uses it on connect to tell a maker their firmware is behind, or that the board
  is not the one selected, on every board.
- **Validation:** a host test of the CAPS line (`make test-caps`, in `make check` and CI);
  the Nordic DK and UNO R4 builds.

## 0.3.0 - 2026-10-13T20:30+02:00 - The RGB LED's pins

- **Three pins for the RGB LED:** the IDE's RGB LED (`RGB <colour>`, logical pins 47,
  3, 46) reaches D0, D1 and A5 on the UNO R4 WiFi, and GPIO 47, 3, 46 on the ESP32-S3
  (identity map, no change there). The Nordic DK has no three free pads: those pins go
  nowhere instead of being passed through raw to P1.15, P0.03 (the analog input) and
  P1.14 (the mic).
- **Validation:** the host build and test suite (`make check`); the UNO R4 and Nordic
  DK builds. Not yet flashed.

## 0.2.0 - 2026-10-13T18:00+02:00 - A blue LED

- **A fourth LED pin:** the IDE's blue LED (`LED blue, on`, logical pin 18) reaches a
  pad on every board: GPIO18 on the ESP32-S3 (no firmware change needed there), A4 on
  the UNO R4 WiFi, and the DK's on-board LED3 (P1.28) on the Nordic nRF54LM20 DK. Until
  a board runs this build, a blue LED on the UNO R4 stays dark and on the DK the write
  goes to the unmapped raw pad P0.18.
- **Published 2026-10-13:** the textochip site serves this build (`VER b595b3b`) for the
  Nordic DK (A and B targets, NCS v3.4.0) and the UNO R4 WiFi (upstream Zephyr
  `ef440e43c`).
- **Validation:** the host build and test suite (`make check`); the three board builds.
  Not yet flashed: the bench check of the two new pads is still to do.

## 0.1.0 - 2026-10-07T21:11+02:00 - Baseline

- **Runs IDE programs:** the runtime runs the Chip BASIC bytecode produced by the IDE
  (`SPEC.md`, contract version 2), the same program the in-browser simulator runs,
  received over the serial protocol (`PING`, `LOAD`, `RUN`, `STOP`, `OVERRIDE`, `SAVE`).
- **Build version check:** `VER` answers with the build id, so the IDE can tell a maker
  their board is behind.
- **Supported boards:** bench-verified on the ESP32-S3 and the Nordic nRF54LM20 DK. The
  Arduino UNO R4 WiFi builds and can be installed from the IDE through its bootloader,
  but has not yet been flashed or smoke-tested on a real board.
- **Runs without a PC:** a saved program survives a reboot and runs on its own with the
  PC unplugged.
- **Sensors and actuators:** drives LEDs, buttons, buzzer, servo, motors, analog inputs
  and an ultrasonic distance sensor.
- **Safe stop:** motors and buzzer stop on every stop path, so a robot does not keep
  rolling after `STOP`.
- **Voice and camera:** on the Nordic DK, `VOICE()` reacts to spoken words, and `SEE()`,
  `SEEX()` and `SEESIZE()` report a colour with its position and size from a live camera
  frame.
- **Open source:** released under Apache-2.0, with a host build that runs and tests the
  VM without any board.
- **Validation:** baseline written from the repository on 2026-10-10, not a release
  check.
