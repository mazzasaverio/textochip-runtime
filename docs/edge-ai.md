# Edge-AI on the runtime — the firmware side

> **Status: the full on-device voice path is BUILT — mic capture, the background inference
> service, and the whole board firmware compile for the ESP32-S3; host-proven end-to-end with
> the real model.** The firmware VM runs the `AISTART`/`INFER` opcodes, the C feature extractor
> matches training exactly, TFLite Micro is wired (a `third_party` submodule), `ai_infer`
> classifies real synthetic-speech words, and `hal::aiCapture` (I2S) + `ai_service`
> (capture → features → ai_infer → the VM's class register) turn it into a live `VOICE()` —
> `make test-ai-service` drives the whole chain (TTS speech → the class → the motors) with no
> hardware. The training half lives in
> [`textochip-ml`](https://github.com/mazzasaverio/textochip-ml). The bytecode ISA + serial
> protocol in [`SPEC.md`](../SPEC.md) stay backward-compatible; these are additive (contract
> **version 2**). The ESP32-S3 firmware now **links real TFLite Micro inference** — our vendored
> TFLM (reference kernels) compiles for xtensa via Zephyr's tflite-micro glue and `ai_host.cpp` is
> the board `ai_infer` (`west build` green; the interpreter/model are in the ELF). What remains is
> board-only: validate the INMP441 I2S wiring/format and the on-chip run on the bench (ESP-NN is an
> optional later speedup behind the same `ai_infer`).

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
make ai-infer  →  classes=5  "go"->1  "left"->2  "right"->3  "stop"->4
                  OK: ai_infer end-to-end — TTS speech -> features.c -> TFLM -> correct word
```

- **The model is a REAL keyword spotter — trained on synthetic TTS speech, no mic.**
  textochip-ml's `make_voice_model.py` speaks **go/left/right/stop** in 9 Piper voices (+
  noise/filler "background"), augments, trains a small int8 CNN on OUR MFCC → **95.5% test
  acc** (`models/voice-v1`, labels [background, go, left, right, stop] = 0..4 = the
  `VOICE_LABELS` convention). `model.h` is vendored here; the test feeds held-out TTS of each
  word (`voice_test_samples.h`) over a representative noise floor. The
  microWakeWord/openWakeWord approach: custom keywords with **zero recording**. (A tone-burst
  `make_demo_model.py` first proved the plumbing.)
- **`src/ai/ai_host.cpp`** is the TFLite-Micro backend of `ai_infer` (`src/ai/ai.h`): it
  loads the int8 `model.h`, quantizes the float MFCC to the model's input scale, runs the
  `MicroInterpreter`, returns the argmax (with `ai_infer_conf` collapsing low-confidence windows
  to 0 = none). One op resolver covers this model *and* the production `kws_cnn`: Conv2D /
  DepthwiseConv2D / pool / MEAN / **REDUCE_MAX** (GlobalMaxPooling) / FullyConnected / Softmax +
  the SUB/MUL/ADD a baked-in input Normalization lowers to.
- **The firmware VM runs the opcodes.** `AISTART`/`INFER` are in `src/isa.*` + `src/vm.cpp`:
  `INFER` pushes the VM's `aiClass` register **and flags the model as wanted** — so a program
  that uses `VOICE()` starts the listening service on its own (the product compiles `VOICE()` to
  a bare `INFER`, with no explicit `AISTART`). `make test-ai-vm` proves a program branches on the
  class; `make test-ai-move` proves the configurator's voice program drives the motors (`MOVE`)
  per keyword — the firmware twin of the product's simVm test.
- **Build:** TFLM is the `third_party/tflite-micro` **submodule**; `make ai-infer` works out of
  the box (`make tflm-lib` builds the lib first). Per-board accel: ESP-NN on ESP32, CMSIS-NN on
  the nRF54L M33, both wired in the Zephyr build.

**Built now — the mic capture + the service.** `hal::aiCapture` (I2S, `zephyr/src/hal_zephyr.cpp`
+ the `i2s0` node in the board overlay) reads the INMP441; `src/ai/ai_service.cpp` runs
`hal::aiCapture → features.c → ai_infer → vm.setAiClass` over a rolling 1 s window, driven by
`runtime::tick` between VM ticks (non-blocking). The **whole board firmware compiles for the
ESP32-S3** (`west build`), and `make test-ai-service` runs the entire chain on the host with the
real model (TTS speech → the detected class → the voice program → `MOVE`).

**On-device inference — BUILT.** The board build compiles our vendored `third_party/tflite-micro`
(reference kernels) through Zephyr's `modules/tflite-micro` glue (pointed at the submodule; the
third-party deps come from `tools/make/downloads/`) and links `ai_host.cpp` — the same portable TFLM
backend the host tests use — as the board `ai_infer`. `west build -b esp32s3_devkitc/esp32s3/procpu`
is **green** (FLASH ~4.9%, DRAM ~61% with the 48 KB arena; `MicroInterpreter`/`ai_infer` in the ELF).
No separate `ai_esp32.cpp`: ESP-NN is a build-time kernel swap, not different code.

**What's left (board-only, for the bench):** confirm the INMP441 wiring — the I2S pins in
the board overlay (BCK/WS/SD) + the 24-bit-in-32-bit slot format in `aiCapture` — and validate the
on-chip inference run on the real board (`IF VOICE()="go"` reading the live mic). The host proves the
identical code + model classify correctly; ESP-NN is an optional later speedup.

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

## ISA additions (Tier 4 — edge-AI) — implemented in the VM

| Instruction        | Meaning                                                            |
|--------------------|-------------------------------------------------------------------|
| `AISTART <model>`  | flag that the program wants `<model>` running; the board's background inference service then keeps the class register fresh. ✅ in `vm.cpp` |
| `INFER <model>`    | push the **latest** class index for `<model>` (`0` = none) onto the value stack — **non-blocking**, like `AREAD`. ✅ in `vm.cpp` |

The browser compiler maps the readable function `VOICE()` → emits `INFER voice` at the use
site; word-values (`="stop"`) resolve to the class index from the model's `labels.json` (the
product's `VOICE_LABELS` mirrors it). `AISTART` is in the ISA for the firmware-managed start of
the listening service. Inference never blocks the tick loop: the service runs between ticks and
`INFER` only reads the freshest `aiClass`. The host `make test-ai-vm` exercises this end of the
contract.

## What lives where (`src/ai/`)

```
src/ai/
  ai.h            the ai_infer interface (int ai_infer(features, n) -> class index)        ✅
  ai_service.cpp  the background service: hal::aiCapture -> features -> ai_infer -> class   ✅
  ai_host.cpp     the TFLite-Micro backend of ai_infer — host tests AND the ESP32-S3 board  ✅
  features.c/.h   the on-device MFCC — matches the training contract (golden vectors)      ✅
  models/voice/   the vendored artifact from textochip-ml (model.h, labels.json)           ✅
  ── vision (SEE()) — the same shape, one sense apart ──
  ai_vision.h        the vision interface (int ai_infer_vision(image, n) -> class)         ✅
  vision_service.cpp the camera service: hal::camCapture -> ai_infer_vision -> class        ✅
  ai_vision_tflm.cpp TFLM person-detection backend (Phase-0 vision stand-in; host proof)    ✅
  ai_vision_stub.cpp no-model vision fallback (0=nothing) — the board build's backend        ▶
third_party/tflite-micro   the TFLM submodule (built into libtensorflow-microlite.a)       ✅
```

**The HAL capability + the service — BUILT:**

```cpp
// src/hal.h: the one new per-board function — read a mic window (non-blocking).
namespace hal { int aiCapture(int16_t* out, int n); }  // I2S mic (INMP441) -> n samples
```

The **background inference service** (`src/ai/ai_service.cpp`) ties it together: between VM ticks,
when `vm.aiRequested()`, `runtime::tick` runs `hal::aiCapture → features.c → ai_infer →
vm.setAiClass(class)` over a rolling 1 s window (`TEXTOCHIP_AI` guards the drive so the plain host
demo stays dep-free). The host feeds PCM to the same path (`make test-ai-service`) or injects the
class directly (`make test-ai-vm` / `test-ai-move`); on the board `hal::aiCapture` reads the I2S
mic (`zephyr/src/hal_zephyr.cpp`). Exactly one `ai_*` backend links per build: the **ESP32-S3 board
build links `ai_host` with real TFLM** (Zephyr's tflite-micro glue over our submodule — see
`zephyr/CMakeLists.txt`), and the host voice tests link `ai_host` too. ESP-NN is an optional
kernel-level speedup behind the same `ai_infer`.

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

- **Phase 0 — plumbing.** ✅ The feature contract (C MFCC = golden vectors), the TFLM build,
  and `ai_infer` driving a real model — all host-proven.
- **Phase 1 — our words.** ✅ `go/left/right/stop` trained from synthetic TTS (`voice-v1`,
  95.5%), run through the firmware `ai_infer` on the host; the VM `AISTART`/`INFER` opcodes
  branch on the result; the mic-capture HAL + inference service are **built + host-proven**
  (`make test-ai-service`, the full chain to `MOVE`), and the ESP32-S3 board build now **links
  real TFLM inference** (`west build` green — the model + interpreter in the ELF). *Remaining:* the
  mic bring-up + on-chip validation on **real hardware**, then the nRF54L (CMSIS-NN on the M33; the
  Axon NPU as the optional accelerator).
- **Phase 2 — vision.** 🚧 Scaffolded + host-proven: `SEE()` (→ `INFER vision`), the camera
  service (`vision_service.cpp`) + a per-model VM register (`visionClass`), and a real Phase-0
  model — TFLM **person-detection** (`make test-vision`: a real image → the class → `SEE()`
  branches, no camera). Next: the ESP32-S3-CAM capture over the DVP camera interface + a trained
  vision-v1 (person/ball/hand) via MobileNet transfer learning — same contract as voice.

See [`textochip-ml/docs/pipeline.md`](https://github.com/mazzasaverio/textochip-ml/blob/main/docs/pipeline.md)
for the training side.
