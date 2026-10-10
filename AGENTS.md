# textochip-runtime

## Goal and customers

The open firmware of Text to Chip (Apache-2.0, public on GitHub): a small bytecode VM on
Zephyr that runs on the board the Chip BASIC programs compiled by the `textochip` IDE,
and keeps running a saved program with no PC and no cloud. Its users: makers and
educators who install it on their boards (from the IDE or a terminal), the `textochip`
IDE and compiler that talk to it through `SPEC.md`, and contributors who port it to new
boards. The product's goal, customers, stage and current bet are in `textochip`'s
direction documents (see "Where things are"); this repo has none of its own.

## Stack

C++17 and C11 on Zephyr RTOS, built with `west`: upstream Zephyr and the Zephyr SDK for
the ESP32-S3 and the UNO R4, the nRF Connect SDK (v3.4.0 in `scripts/publish-hex.sh`) for
the Nordic DK. Edge AI: TensorFlow Lite Micro (submodule `third_party/tflite-micro`), or
Nordic's nRF Edge AI add-on on the nRF54LM20B's Axon NPU (external, never vendored).
Host build and tests: plain g++/gcc and make (`host/Makefile`). No database, no web
service, no secrets.

## Delivery targets

- Firmware images for the reference boards: ESP32-S3 DevKitC, Nordic nRF54LM20 DK (A
  and B chips), Arduino UNO R4 WiFi (build-proven only, bench pending:
  `docs/progress.md`). What each one supports: `README.md`, "Status".
- The Nordic DK hex files reach makers through the `textochip` site (see "Production");
  the UNO R4 image is installed by the IDE through the board's Arduino bootloader
  (`README.md`, "Arduino UNO R4 WiFi").
- The source, for contributors who build it or port it (`CONTRIBUTING.md`).

## Layout

```
src/          portable core shared by every target, no hardware calls: isa, vm,
              runtime (serial protocol), hal.h (the only per-board interface),
              ai/ (voice and vision services, two classifier backends), legacy missions
host/         PC build and the test suite; hal_host fakes the hardware
zephyr/       Zephyr application: src/hal_zephyr.cpp, prj.conf, Kconfig,
              boards/ (per-board .conf and .overlay)
scripts/      publish-hex.sh (publishes the Nordic hex), hooks/pre-push (its gate)
third_party/  tflite-micro submodule: upstream code, never edited here
docs/         changelog, log, technical decisions, hardware and bring-up notes
SPEC.md       the bytecode ISA and serial protocol: the contract with textochip
```

## Commands

- `make check`: host build, the secret-free host test suite (the same steps as
  `.github/workflows/ci.yml`) and `git diff --check`, about 20 seconds. Run it before
  declaring any work done and before every push to main.
- `make check-fast`: host build (it links the whole runtime) and `git diff --check`,
  after every edit (the Claude hook runs it).
- `make dev`: the host demo (`make -C host run`): the same bytecode the IDE produces, run
  on the PC with no board.
- Single host tests, and the ones that need the TFLite Micro submodule built (`tflm-lib`,
  `ai-infer`, `test-ai-service`, `test-vision`): `host/Makefile`, `CONTRIBUTING.md`.
- Board builds and flashing (`west build -b <board> zephyr`): `README.md`, "Build for
  hardware". They are not in `make check`: they need the Zephyr or NCS toolchains, and the
  B target needs Nordic's sdk-edge-ai checkout (`~/projects/labs/sdk-edge-ai` on this
  machine; `TEXTOCHIP_EDGEAI_DIR` overrides it).

## Production

- No Coolify application: a push to `main` publishes the source on GitHub and nothing
  else reaches the boards.
- The Nordic hex reaches makers only through `scripts/publish-hex.sh`: from a clean
  commit it builds the A and B targets, copies both hex files and their `.version` into
  `textochip/public/firmware/`, then commits and pushes `textochip` main, which deploys
  the site. Running it is a production release of `textochip`.
- A republished hex needs a bench check on the DK first (`docs/DECISIONS.md`,
  2026-10-07).
- The firmware answers `VER` with its build id (the short commit), so the IDE can tell a
  maker their board is behind.

## Design

No UI in this repo. The IDE, its design system and every user-facing screen live in
`textochip`.

## Project conventions

- The board executes bytecode; it never parses BASIC.
- A saved program never depends on a PC or a cloud connection to run.
- VM waits, inference and services are cooperative: they never block the control
  channel, so `STOP` and `OVERRIDE` stay responsive.
- The VM zeroes motors and buzzer on every stop path (STOP, HALT, end, error): a robot
  must not keep rolling after `STOP`.
- The ISA and the protocol evolve additively unless a new contract version is explicit.
  An opcode or protocol change updates `SPEC.md` and `textochip`'s compiler and simulator
  in the same task; outside contributors open an issue first.
- Board-specific work stays behind `src/hal.h`, in `zephyr/boards/` and the pin map in
  `zephyr/src/hal_zephyr.cpp`.
- A change in VM or parser behaviour comes with a focused host test.
- Hardware claims say whether they are host-proven, build-proven or bench-proven; a board
  is supported only with a real build and, where relevant, a bench result.
- Open core: the runtime stays usable without the private services (IDE, AI assistant,
  model training). Nordic's proprietary components are referenced at build time, never
  vendored; third-party material is recorded in `THIRD_PARTY_NOTICES.md`. Never commit
  build directories, generated firmware, proprietary SDK files or datasets.
- The `CALL` opcode and the native mission registry stay, although the product retired
  missions, so previously saved bytecode keeps running.
- Living documents (`README.md`, `SPEC.md`, `ARCHITECTURE.md`, `docs/bench-runbook.md`)
  are updated in place; `docs/nordic-nrf-connect-sdk.md` and `docs/LOG.md` get dated
  entries.

## Traps

- `src/ai/features.c` must match `textochip-ml`'s training feature extraction:
  `make -C host test-ai` checks it against the golden vectors. A drift silently degrades
  voice recognition on every board.
- The SEE and SNAP bench commands in `runtime.cpp` call `vision_service` outside the AI
  ifdef: every binary that links `runtime.cpp` also needs `VISION_OBJS`
  (`host/Makefile`). The host build broke this way once and nobody noticed for weeks.
- The publish gate is off on this machine: `scripts/hooks/pre-push` runs only with
  `git config core.hooksPath scripts/hooks`, and this clone points `core.hooksPath` at
  `core/githooks`. Both scripts also default to old paths (`~/projects/textochip`,
  `~/projects/products/textochip`): run `scripts/publish-hex.sh` with
  `TEXTOCHIP_PRODUCT_DIR=~/workspace/products/textochip`.
- UNO R4 WiFi: RAM is the binding constraint (31.1 of 32 KB). Programs are capped at
  128 instructions (`CONFIG_TEXTOCHIP_MAX_PROGRAM`) and the IDE warns at the same cap:
  change both together. Logical pin 4 also drives D13, the "L" LED.
- Nordic DK: a saved program persists through `flash_area` on RRAM, not NVS (NVS does
  not stick there); pads `P1.01` and `P1.02` are shorted to ground; the camera SPI must
  run at 8 MHz; flash with the `jlink` runner, not plain probe-rs.
- ESP32-S3: flash on the "USB UART" port, connect the IDE on the "USB OTG" port. The
  build overflows DRAM on local upstream Zephyr 4.4.99 (pre-existing, open in
  `docs/progress.md`).

## How work flows

- Tasks are GitHub issues in this repo, labelled with a role (`role:researcher`) and a
  track: `bet:<n>` for the current bet (in `textochip/docs/03-roadmap.md`), `run` for
  keeping the product alive. At most two in progress (`doing`), normally one per track;
  `waiting` means a decision for the owner. The `operating-lead` skill keeps the queue
  (`core/holding/02-ceo-manual.md`, "How work flows").
- A pull request that changes something for the users (makers, the IDE, contributors)
  adds its entry to `docs/CHANGELOG.md` (`stack-rules`, rule 18).
- Work left half done: `docs/progress.md` (done, to do, next step), deleted when done.

## Related repositories

Text to Chip is one product split into four repos, cloned side by side in
`~/workspace/products/` (the `p`/`px` picker gives the agent the siblings as `--add-dir`).
Before changing a shared contract, read the other side and update both in the same task.

- `textochip`: the product. Next.js IDE, Chip BASIC compiler (`lib/compiler/`),
  simulator, landing page, and the product's direction documents. The compiler and
  simulator implement this repo's `SPEC.md`: an opcode or protocol change lands in both
  repos. It also hosts the published Nordic hex (`public/firmware/`).
- `textochip-api`: internal FastAPI service, natural language to Chip BASIC.
- `textochip-runtime` (this repo): Zephyr bytecode VM firmware, the open part.
- `textochip-ml`: edge AI models; its int8 `model.h` is consumed by `src/ai/`, and
  `src/ai/features.c` must match its training feature extraction.

## Where things are

- Direction: the product's `docs/01-vision.md`, `02-strategy.md`, `03-roadmap.md` and
  `04-decisions.md` are in the `textochip` repo and cover the runtime too. This repo has
  no direction documents; `docs/archive/` keeps its former `VISION.md` and `ROADMAP.md`.
- `docs/CHANGELOG.md`: what reached the users, one entry per release (`stack-rules`
  rule 18). The only changelog.
- `docs/LOG.md`: the technical log for developers, newest first, appended each session.
- `docs/DECISIONS.md`: technical and licensing decisions (board ports, architecture,
  the Apache-2.0 open core), newest first. Older architectural rationale is in
  `ARCHITECTURE.md`.
- `docs/research/`: research specific to the runtime (boards, makers, contributors);
  the product's research is in `textochip/docs/research/`.
- `SPEC.md` (the contract), `ARCHITECTURE.md` (how core, HAL and boards fit and why).
- `docs/hardware.md` (Arducam supply rail, UNO R4 WiFi pinout and bench checklist),
  `docs/bench-runbook.md` (wiring and bring-up of the voice robot), `docs/edge-ai.md`
  (the VOICE() and SEE() inference design), `docs/nordic-nrf-connect-sdk.md` (the Nordic
  port, dated notes).
- `CONTRIBUTING.md`, `SECURITY.md`, `THIRD_PARTY_NOTICES.md`, `NOTICE`, `LICENSE`: the
  open-source project files.
- Never add a `CLAUDE.md`: it stops Claude Code from reading `AGENTS.md`.
