# Third-party notices

The root [Apache License 2.0](LICENSE) covers the Text to Chip Runtime source,
documentation and bundled Text to Chip model weights unless a file is
explicitly marked otherwise. External projects and attributed data-derived
fixtures keep their own licences.

## TensorFlow Lite for Microcontrollers

`third_party/tflite-micro` is a Git submodule of
[tensorflow/tflite-micro](https://github.com/tensorflow/tflite-micro), licensed
under Apache-2.0. Its source and licence are not copied into this repository's
Git history; cloning with `--recurse-submodules` fetches the pinned upstream
revision.

## Bundled keyword-spotting model

`src/ai/models/voice/model.h` is a Text to Chip DS-CNN model trained from
[Google Speech Commands v0.02](https://www.tensorflow.org/datasets/catalog/speech_commands),
synthetic speech generated with Piper voices, and local bench-noise
augmentation. The source dataset is distributed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/); the training process
selected four command classes, mixed in generated samples and applied noise and
speed augmentation. The dataset is described in:

> Pete Warden, "Speech Commands: A Dataset for Limited-Vocabulary Speech
> Recognition," arXiv:1804.03209, 2018.

`src/ai/models/voice/voice_test_samples.h` contains generated test audio from
the Piper `en_US-amy-low` voice. The upstream
[voice model card](https://huggingface.co/rhasspy/piper-voices/blob/main/en/en_US/amy/low/MODEL_CARD)
identifies [MycroftAI/mimic3-voices](https://github.com/MycroftAI/mimic3-voices)
as its source dataset, which is licensed under
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/). To preserve the
most conservative downstream terms, that generated test fixture is offered
under CC BY-SA 4.0 rather than the repository's default Apache-2.0 licence. It
was converted to 16 kHz floating-point PCM and split into the four command
labels for this test. Neither the Speech Commands dataset nor a Piper voice
model is redistributed in this repository.

## Nordic nRF Edge AI

The optional nRF54LM20B voice and person-detection backends integrate with
Nordic's `sdk-edge-ai` module and its `LicenseRef-Nordic-5-Clause` components at
build time. No Nordic model, prebuilt library, post-processing source, or
proprietary licence text is vendored here. Users must obtain that module from
Nordic and comply with its terms. The open TFLite Micro backend remains
available without it.

## Zephyr and platform SDKs

Zephyr, the nRF Connect SDK, ESP-NN, CMSIS-NN, board support packages and build
tools are external build dependencies. They are not redistributed by this
repository and retain their upstream licences.
