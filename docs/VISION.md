# Runtime vision

Text to Chip Runtime is the open, auditable layer that turns a small bytecode
program into autonomous behaviour on a microcontroller.

## Who it serves

- Makers and educators who want a board to keep working after the browser is
  disconnected.
- Firmware contributors who want to port the VM to another Zephyr-supported
  board without rebuilding the product stack.
- Hardware partners who need a stable, inspectable execution and transport
  contract.

## Product promise

A program compiled by Text to Chip behaves consistently in the browser
simulator and on supported hardware. Loading is a one-time operation; after a
program is saved, the board owns execution and can run without a PC or cloud.
Long waits, inference and hardware behaviour are cooperative so `STOP` and
runtime overrides remain responsive.

The runtime is deliberately small:

- `SPEC.md` is the stable bytecode and serial-protocol contract.
- `src/` is a platform-independent VM and service layer.
- one HAL implementation contains each board port.
- host tests exercise the portable core without hardware.

## Open-core boundary

This repository is the Apache-2.0 open core: VM, protocol, HAL, reference
hardware implementations, compatibility missions and the bundled inference
artifacts required by the open TFLite Micro path.

The hosted IDE, browser compiler and simulator, AI assistant, curated mission
catalog, model-training lifecycle and commercial hardware kits are separate
products. The runtime must remain usable without those private services.

## What accumulates

The durable asset here is not the dispatcher alone. It is the growing body of:

- bench-verified board ports and wiring knowledge;
- compatibility guarantees around one small ISA and protocol;
- hardware failure modes captured as tests and runbooks;
- reproducible edge-AI integration across constrained chips;
- contributor experience that makes the next port cheaper than the first.

A competitor can recreate a bytecode loop quickly. Recreating verified
behaviour across boards, peripherals, toolchains and years of stored programs is
the compounding advantage.

## Boundaries

- The board executes bytecode; it does not parse BASIC.
- The runtime never depends on a cloud connection to run a saved program.
- VM waits and services do not block the control channel.
- The ISA evolves additively unless a new contract version is explicit.
- Board-specific work stays behind the HAL and board configuration.
- Claims about hardware support require a real build and, where relevant, a
  bench result.
