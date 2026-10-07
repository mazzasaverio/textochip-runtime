
### Arducam bring-up: the DK's supply rail (2026-08-01)

The camera answered nothing on SPI — `CAM` read `id=0x00`. Three probes, in
order, and what each ruled out:

| Probe | Result | What it settles |
|---|---|---|
| `CAMPINS` | `spim22 sck=P1.2 mosi=P1.4 miso=P1.3` | the SPI instance owns exactly the wired pads (`enable=0` at rest is normal — Zephyr powers the instance only during a transfer) |
| `CAMBB` | `bytes=00 00 00` | the same read **bit-banged in plain GPIO**, with the SPI peripheral bypassed, is silent too: the controller was never the problem |
| `RAIL` | `VDD = 3033 mV` | the board runs at **3.0 V**, and Nordic's own Arducam sample for this camera on this DK says in one line to *"configure the development kit using Board Configurator to provide 3.3V to power the camera"* |

The mic is unaffected by the rail (the INMP441 works from 1.62 V), which is why
voice has worked all along while the camera stays mute — a difference that looks
like a camera fault and is not one.

**Action:** nRF Connect for Desktop → *Board Configurator* → set VDD to 3.3 V,
then power-cycle the DK and re-run `CAM`.

`RAIL` and the `CONFIG_ADC=y` + `/zephyr,user` channel behind it are marked
TEMPORARY in the DK overlay: on a board with a real analog sensor that node
belongs to `AREAD`, so it comes out once the rail question is closed.

### Arduino UNO R4 WiFi pinout (2026-10-07, build-proven, not bench-verified)

Logical pin (bytecode) to the Arduino pin printed on the board. Source of truth:
`map_pin()` in `zephyr/src/hal_zephyr.cpp`; the IDE mirrors it in
`lib/boards.ts` (`UNO_R4_PINS`).

- 1 green LED: D2
- 2 yellow LED: D3
- 4 red LED: D4
- 5 buzzer: D5 (PWM, GPT0 channel A)
- 6 button A: D7 (input, pull-up, active low)
- 7 PIR: D8
- 8 servo: D6 (PWM, GPT3 channel A, 50 Hz)
- 9 analog in: A0 (`AREAD`, ADC channel 9, 12-bit)
- 38, 39, 40 line sensors left, center, right: A1, A2, A3
- 41 obstacle sensor: D12
- 42 relay: D9
- HC-SR04 (`DIST`, HAL-owned): TRIG D10, ECHO D11
- Not wired in phase 1: motors (10-14, 21). `MOVE` is a no-op.

Free for later: D0, D1 (upstream's console UART, disabled here), D13 (on-board
"L" LED), A4, A5, the Qwiic connector.

**5 V I/O.** The RA4M1 on this board runs at 5 V, so its outputs are 5 V and its
inputs expect up to 5 V. 3.3 V-only parts (the INMP441 mic, the Arducam) must not
be wired straight to it. An HC-SR04 works without a level shifter here.

**Bench checklist** for the first flash: the IDE's in-browser install through the
USB-C port (Arduino bootloader, see the README), the IDE connects over the same port, `PING`,
LED on D2/D3/D4, button on D7, `TONE` on D5, `SERVO` on D6, `AREAD` on A0, relay
on D9, `DIST` with an HC-SR04 on D10/D11, `SAVE` then a power cycle to check
autorun, and the main stack (4 KB) under a long program.
