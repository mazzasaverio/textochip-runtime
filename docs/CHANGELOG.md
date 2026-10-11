# Changelog

What reached the people and services that use the Text to Chip runtime, newest first,
one entry per release (`stack-rules` rule 18): semantic version, ISO 8601 timestamp
with timezone, a short name, then one bullet per change, each starting with a short
title in bold and a colon, and a last bullet on how it was verified. Its users are the
makers who install this firmware on their boards (from the `textochip` IDE or from a
terminal), the `textochip` IDE and compiler that talk to it through the bytecode and
serial contract in `SPEC.md`, and contributors who build it from source.
Implementation details stay in git and in `LOG.md`.

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
