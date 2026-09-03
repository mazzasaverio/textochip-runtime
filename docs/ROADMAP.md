# Runtime roadmap

Last reviewed: 2026-09-03.

## Released

- A portable, tick-based VM and serial protocol run on the host, ESP32-S3 and
  Nordic nRF54LM20 DK.
- Programs can be saved and started automatically after reboot with the PC
  disconnected.
- GPIO, PWM tone, servo, motors, ADC, ultrasonic distance, voice and vision are
  represented behind the HAL and bytecode contract.
- The open TFLite Micro inference path and optional Nordic Axon integration are
  implemented. Nordic proprietary components are referenced at build time and
  are not vendored.
- The repository is licensed under Apache-2.0, documented for contributors and
  checked by secret-free host CI.

## Now: reproducible community hardware builds

**Outcome:** A contributor with a clean checkout can build the fully open
ESP32-S3 and nRF54LM20A targets, understand why the optional B-chip Axon targets
need an external Nordic module, and produce a useful failure report.

**Scope:** Pin the supported toolchain inputs, reconcile build instructions,
exercise both open board configurations from clean workspaces, and turn any
implicit local dependency into an explicit prerequisite or automated check.

**Definition of done:**

- The documented ESP32-S3 and nRF54LM20A commands succeed from clean checkouts.
- The host CI remains green without credentials or proprietary downloads.
- A failed optional Axon build names the missing external module and points to
  its licence boundary.
- Each open target has a recorded flash-and-boot smoke test.

**Validation:** Clean-clone builds, host CI, firmware boot, `PING`/`PONG`, and a
load/run/stop smoke program on each board.

**Non-goals:** Reproducing Nordic's proprietary SDK components, adding a new
board, or changing the ISA.

## Next: versioned runtime releases

**Outcome:** Product builds and external users can identify, download and verify
an immutable runtime release instead of relying on an unnamed branch snapshot.

**Scope:** Semantic tags, release notes, checksums, reproducible open-target
artifacts and an explicit compatibility statement for the ISA/protocol version.

**Definition of done:** A tagged release contains verified artifacts for the
open targets, their checksums and build provenance; the firmware `VER` response
maps unambiguously to that release.

**Validation:** Install each published artifact, verify its checksum and version,
then run the protocol smoke test.

**Non-goals:** A hosted firmware builder or automatic support for arbitrary
boards.

## Later

- Measure power on real hardware, then decide whether light sleep or resumable
  deep sleep earns its complexity for scheduled programs.
- Accept another board port only when its HAL, CI coverage and bench evidence
  meet the existing support bar.
- Add parser fuzzing and long-running control-channel stress tests before the
  protocol surface materially expands.
