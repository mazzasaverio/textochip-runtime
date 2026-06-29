# Edge-AI on the runtime — the firmware side (design, planned)

> **Status: design / contract draft (not yet implemented).** This is the firmware half
> of the edge-AI pipeline; the training half lives in
> [`textochip-ml`](https://github.com/mazzasaverio/textochip-ml). It specifies the ISA
> additions, the HAL extension, the on-device feature extractor, and the data forwarder
> so the two repos agree before any C++ lands. The bytecode ISA + serial protocol in
> [`SPEC.md`](../SPEC.md) stay backward-compatible; these are additive (a future
> contract **version 2**).

## The idea

A trained model becomes a **native mission**: the board listens/looks on-device,
offline, and a child branches on the result in Chip BASIC —

```basic
10 IF VOICE()="stop" THEN MISSION "SEMAFORO"
20 GOTO 10
```

`textochip-ml` trains a small int8 model and exports a 4-file artifact; this runtime
compiles it in and runs it behind one HAL function — on **two** chips from the **same**
`.tflite`.

## One model, two backends (the portability that justifies it)

The exported `model_int8.tflite` is consumed by both targets; only the backend behind
`hal::aiInfer` differs (the same per-board HAL pattern the rest of the runtime uses):

| Target | Backend | Model form |
|--------|---------|-----------|
| **ESP32-S3** | TFLite Micro + **ESP-NN** kernels (CPU/vector) | `model.h` C-array `#include`d |
| **Nordic nRF54L** | **Axon NPU** via `nrf_edgeai_lib` (≈15× faster / ≈10× lower energy) | `model_int8.tflite` → Axon compiler → `.h` |

Keep models in the operator subset **both** accelerate: conv / depthwise / pooling /
fully-connected / ReLU / softmax (the model side enforces this in
`textochip-ml/models/kws_cnn.py`).

## ISA additions (Tier 4 — edge-AI, planned)

| Instruction        | Meaning                                                            |
|--------------------|-------------------------------------------------------------------|
| `AISTART <model>`  | start the background inference service for `<model>` (compiler **prologue**, like `MODE`): the mission captures windows and runs the model continuously, updating a "last class" register. |
| `INFER <model>`    | push the **latest** class index for `<model>` (or `0`/none) onto the value stack — **non-blocking**, like `AREAD`. |

The browser compiler maps the readable function `VOICE()` → emits `AISTART voice` in
the prologue and `INFER voice` at the use site; word-values (`="stop"`) resolve to the
class index from the model's `labels.json`. Inference must never block the tick loop,
so it is a *background service the mission drives* and `INFER` only reads the freshest
result.

## HAL extension (one new capability per board)

Add to `src/hal.h` (planned), implemented once per board exactly like the existing ~10
functions:

```cpp
// Edge-AI inference (planned, Tier-4). The model is compiled into the firmware
// (ESP32: TFLM+ESP-NN C-array; nRF54L: Axon-compiled model on the NPU).
namespace hal {
  void aiStart(const std::string& modelId);            // begin background capture+inference
  int  aiInfer(const std::string& modelId);            // latest class index, or -1 if none yet
  int  aiCapture(const std::string& modelId, int16_t* buf, int n);  // raw mic/sensor window
}
```

The portable, board-agnostic pieces live in `src/ai/`:

```
src/ai/
  ai.h            the mission base for AI missions (capture → features → aiInfer → register)
  features.c/.h   the on-device feature extractor — MUST match the training MFCC
  missions/voce.cpp   the VOCE native mission
models/<name>-v<n>/   the artifact vendored from textochip-ml (model.h, labels.json, …)
```

## The feature-extraction contract (the #1 footgun)

`src/ai/features.c` re-implements `textochip-ml/src/textochip_ml/features/mfcc.py`. The
on-device features **must equal** the training features, or accuracy collapses silently.
Two guards make this a *test*, not a surprise:

1. Every model ships `feature_params.json` (sample rate, frame/hop, n_fft, n_mels,
   n_mfcc, …); `features.c` reads the same numbers — no hard-coded defaults.
2. The host build checks `features.c` against the **shared golden vectors**
   `textochip-ml/tests/golden/mfcc_golden.json` (same deterministic signal, same
   expected MFCC, within tolerance). The Python `test_feature_parity.py` checks the
   other end of the same contract.

## Data forwarder (DIY data collection over USB-CDC)

Phase-1 training data is captured from the board's mic over the existing USB-CDC link —
no cloud, no phone app. A simple addition to the serial protocol (planned):

| From the host (`textochip-ml/scripts/collect_data.py`) | Board action | Reply |
|--------|--------------|-------|
| `REC <ms>` | capture `<ms>` of mic audio at the model sample rate | `OK: rec <n_samples>` then `<n_samples>` int16-LE samples |

## Phases (mirrors `textochip-ml`)

- **Phase 0 — plumbing, zero training.** Wire `AISTART`/`INFER` + the VOCE mission +
  `hal::aiInfer`, deploy an existing `micro_speech` (yes/no) model. Prove the BASIC
  branch on the **host** build, then on the **ESP32**, then run the **same `.tflite`**
  through the Axon compiler on the **nRF54L** — portability proven before training ours.
- **Phase 1 — our words.** Italian vocabulary (`rosso`/`verde`/`avanti`/`stop`),
  trained in `textochip-ml`, redeployed to both boards.
- **Phase 2 — vision.** MobileNet transfer learning (ESP32-CAM), same contract.

See [`textochip-ml/docs/pipeline.md`](https://github.com/mazzasaverio/textochip-ml/blob/main/docs/pipeline.md)
for the training side.
