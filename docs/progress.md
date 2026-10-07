# Progress: Arduino UNO R4 WiFi port

Spec: `docs/DECISIONS.md`, entry 2026-10-07. Pinout: `docs/hardware.md`.

## Done (2026-10-07)

- Runtime: board `.conf` and `.overlay`, Arduino pin table in `hal_zephyr.cpp`,
  blob store on the data flash, servo on its own GPT, relay D9, HC-SR04 D10/D11,
  per-board `CONFIG_TEXTOCHIP_MAX_PROGRAM` (R4: 128), AI-off build links.
- Checks: R4 build OK (90.2 KB flash, 31.1 KB RAM); 12 host tests green;
  nRF54LM20A build OK before and after (same RAM). ESP32-S3 overflows DRAM by
  31404 B on local Zephyr 4.4.99 before and after: pre-existing, not this change.
- IDE (`textochip`): board `unor4wifi` in the selector (status incoming, flash
  card "pending"), per-board cap, warnings for VOICE()/SEE() and MOVE, no mic,
  camera or motor wiring steps, Arduino USB vendor ID in the port picker.

## To do

1. Bench (board arrives 2026-10-08): run the checklist in `docs/hardware.md`.
   First unknown: does `west flash` (pyOCD over the bridge's CMSIS-DAP) work?
2. Decide how a maker flashes the R4 without a toolchain, then replace the IDE's
   "pending" flash card.
3. After the bench: drop `provisionalWiring` and set status `ready` in the IDE.
4. Rebuild and publish the Nordic hex at the next bench session with the DK.
5. ESP32-S3 DRAM overflow on upstream Zephyr main: find which Zephyr revision the
   published ESP32 firmware is built with.

## Next step

Flash the board and run `PING` from the IDE's serial connect.
