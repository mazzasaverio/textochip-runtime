
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
