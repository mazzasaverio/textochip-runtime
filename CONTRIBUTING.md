# Contributing

Thanks for helping make the Text to Chip Runtime more portable and dependable.

## Start here

1. Read [the runtime vision](docs/VISION.md), [the architecture](ARCHITECTURE.md)
   and [the bytecode contract](SPEC.md).
2. Open an issue before making an ISA or serial-protocol change. Compatibility
   is part of the product contract.
3. Fork the repository, create a focused branch and submit a pull request.

## Local checks

The host build is the fastest feedback loop and needs only a C++17 compiler:

```bash
make -C host textochip_vm_host
make -C host test-idle
make -C host test-dist
make -C host test-ai-vm
make -C host test-ai-move
make -C host test-color
make -C host test-color-move
make -C host test-color-service
make -C host test-ai
```

Tests that execute TFLite Micro also need the pinned submodule:

```bash
git submodule update --init --depth 1 third_party/tflite-micro
make -C host tflm-lib
make -C host ai-infer
make -C host test-ai-service
make -C host test-vision
```

Run `git diff --check` before submitting. CI repeats the secret-free host suite
on every pull request.

## What a change needs

- VM or parser behaviour needs a focused host test.
- An ISA or protocol addition must update `SPEC.md` and the browser-side contract
  in the `textochip` repository.
- A board port belongs behind `hal.h`, includes build instructions and records a
  real flash-and-smoke-test result.
- A hardware claim must distinguish host-proven, build-proven and bench-proven.
- Meaningful changes update the affected living documentation in the same pull
  request. Historical entries are appended, not rewritten.

Do not commit build directories, generated firmware, credentials, proprietary
SDK files, datasets or components whose redistribution terms are unclear.

## Licence

Unless explicitly stated otherwise, contributions submitted for inclusion are
licensed under the repository's [Apache License 2.0](LICENSE), as described in
section 5 of that licence. Third-party material must keep its original notices
and be recorded in `THIRD_PARTY_NOTICES.md`.
