# Edge-AI on the runtime — the firmware side (design, planned)

> **Status: design / contract draft (not yet implemented).** This is the firmware half
> of the edge-AI pipeline; the training half lives in
> [`textochip-ml`](https://github.com/mazzasaverio/textochip-ml). It specifies the ISA
> additions, the HAL extension, the on-device feature extractor, and the data forwarder
> so the two repos agree before any C++ lands. The bytecode ISA + serial protocol in
> [`SPEC.md`](../SPEC.md) stay backward-compatible; these are additive (a future
> contract **version 2**).

## Spike results — TFLM + the feature contract are host-proven (2026-06-29)

The two integration risks are both **de-risked with running code on the host**:

1. **The feature extractor matches training.** `src/ai/features.c` (a portable C MFCC)
   reproduces textochip-ml's Python golden vectors **exactly** — `make test-ai` →
   *"OK: C MFCC matches the Python golden vectors (frames=49, sum=-3402.9147, tol=0.010)"*.
   The #1 TinyML footgun (train/inference feature drift) is now a build-time check on
   both sides (Python `test_feature_parity.py` + firmware `make test-ai`, same
   `mfcc_golden.json`).
2. **TFLite Micro builds with our toolchain and our own code drives it.** The upstream
   `tflite-micro` builds clean with **g++ 13.3** (it vendors flatbuffers / gemmlowp / ruy
   / kissfft and produces `libtensorflow-microlite.a`); a minimal program *we* wrote
   (`GetModel → MicroMutableOpResolver → MicroInterpreter(arena) → AllocateTensors →
   Invoke → output`) ran a real int8 model — *"TFLM Invoke ran from our code, arena used
   1184 bytes"*. So the firmware `ai_infer` is a known quantity, not a question mark.

**Link recipe (host):** compile with `-DTF_LITE_STATIC_MEMORY` and
`-I<tflite-micro> -I.../flatbuffers/include -I.../gemmlowp -I.../ruy -I.../kissfft`, link
`libtensorflow-microlite.a`. **Vendoring:** add `tflite-micro` as a git **submodule** under
`third_party/` (or use its source-tree generator); wire the lib into `host/Makefile` and the
Zephyr `CMakeLists.txt` (ESP-NN on ESP32 / CMSIS-NN on the nRF54L M33 — both Apache-2.0).

## Phase 1 — `ai_infer` runs a real model end-to-end (2026-06-30, host-proven)

The full on-device path now **runs and classifies correctly** on the host:

```
make ai-infer  →  classes=3  "go"->1 (want 1)  "stop"->2 (want 2)
                  OK: ai_infer end-to-end — TTS speech -> features.c -> TFLM -> correct word
```

- **The model is a REAL keyword spotter — trained on synthetic TTS speech, no mic.**
  textochip-ml's `make_voice_model.py` speaks go/stop in 9 Piper voices (+ noise/filler
  "background"), augments, trains a small int8 CNN on OUR MFCC → **96.7% test acc**
  (`models/voice-v1`, labels [background, go, stop] = 0/1/2). `model.h` is vendored here; the
  test feeds held-out TTS "go"/"stop" (`voice_test_samples.h`). The microWakeWord/openWakeWord
  approach: custom keywords with zero recording. (A tone-burst `make_demo_model.py` first proved
  the plumbing.)
- **`src/ai/ai_host.cpp`** is the TFLite-Micro backend of `ai_infer` (`src/ai/ai.h`): it
  loads the int8 `model.h`, quantizes the float MFCC to the model's input scale, runs the
  `MicroInterpreter`, returns the argmax (with `ai_infer_conf` collapsing low-confidence windows
  to 0 = none). One op resolver covers this model *and* the production `kws_cnn`: Conv2D /
  DepthwiseConv2D / pool / MEAN / **REDUCE_MAX** (GlobalMaxPooling) / FullyConnected / Softmax +
  the SUB/MUL/ADD a baked-in input Normalization lowers to.
- **Build:** `make ai-infer TFLM_DIR=<a built tflite-micro>`. Productionize by adding
  `tflite-micro` as a `third_party/` **submodule** and wiring its lib into `host/Makefile`
  (done) + the Zephyr `CMakeLists.txt` (ESP-NN on ESP32 / CMSIS-NN on the nRF54L M33).

**What's left:** wire `ai_infer` to the `AISTART`/`INFER` opcodes (the VM `INFER` reads the
class register) + the I2S mic capture, then on **real hardware** (`IF VOICE()="go"` reading the
live model). The inference itself — the part that was a question mark — is now proven.

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

| Target | Backend | License | Model form |
|--------|---------|---------|-----------|
| **ESP32-S3** | **TFLite Micro + ESP-NN** kernels (CPU/vector) | **Apache-2.0** (fully open) | `model.h` C-array `#include`d |
| **Nordic nRF54L — open default** | **TFLite Micro + CMSIS-NN** on the Cortex-M33 (DSP/MVE) | **Apache-2.0** (fully open) | `model.h` C-array `#include`d |
| **Nordic nRF54L — NPU (optional)** | **Axon NPU** via `nrf_edgeai_lib` (≈15× faster / ≈10× lower energy) | 🔴 **proprietary compiler** (Nordic, Docker binary) | `model_int8.tflite` → Axon compiler → `.h` |

**Open-source by default; the NPU is an opt-in accelerator.** The fully-open, portable, efficient
path is **TFLite Micro everywhere** — accelerated by **ESP-NN** on the ESP32 and **CMSIS-NN** on
Nordic's Cortex-M33 (both Apache-2.0). Keyword spotting is the canonical TinyML benchmark and runs in
a few ms on the M33 CPU — it does **not** need the NPU. The **Axon NPU** gives a big speed/energy win
but its compiler is **proprietary** (a closed Docker binary), so we treat it as an **optional** third
backend, most worthwhile for the heavier *vision* tier or extreme low-power. Because all three sit
behind one `hal::aiInfer`, switching CPU↔NPU on Nordic is a **one-file** change — open by default,
proprietary only if you opt in.

Keep models in the operator subset **all** these kernels accelerate: conv / depthwise / pooling /
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
