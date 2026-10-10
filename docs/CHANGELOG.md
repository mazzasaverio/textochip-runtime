# Changelog

What reached the people and services that use the Text to Chip runtime, newest first,
one entry per release (`stack-rules` rule 18): semantic version, ISO 8601 timestamp
with timezone, a short name, what changed for them and how it was verified. Its users
are the makers who install this firmware on their boards (from the `textochip` IDE or
from a terminal), the `textochip` IDE and compiler that talk to it through the bytecode
and serial contract in `SPEC.md`, and contributors who build it from source.
Implementation details stay in git and in `docs/LOG.md`.

## 0.1.0 - 2026-10-07T21:11+02:00 - Baseline

- Runs the Chip BASIC bytecode produced by the IDE (`SPEC.md`, contract version 2),
  the same program the in-browser simulator runs, received over the serial protocol
  (`PING`, `LOAD`, `RUN`, `STOP`, `OVERRIDE`, `SAVE`); `VER` answers with the build id
  so the IDE can tell a maker their board is behind.
- Bench-verified on the ESP32-S3 and the Nordic nRF54LM20 DK; the Arduino UNO R4 WiFi
  builds and can be installed from the IDE through its bootloader, but has not yet
  been flashed or smoke-tested on a real board.
- A saved program survives a reboot and runs on its own with the PC unplugged.
- Drives LEDs, buttons, buzzer, servo, motors, analog inputs and an ultrasonic
  distance sensor; motors and buzzer stop on every stop path, so a robot does not keep
  rolling after `STOP`.
- On the Nordic DK, `VOICE()` reacts to spoken words and `SEE()`, `SEEX()` and
  `SEESIZE()` report a colour with its position and size from a live camera frame.
- Open source under Apache-2.0, with a host build that runs and tests the VM without
  any board.
- Validation: baseline written from the repository on 2026-10-10, not a release check.
