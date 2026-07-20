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

## On-hardware bring-up — the nRF54LM20 DK runs the voice pipeline in real time (2026-07-14 → 15)

First-ever on-device run of the voice tier (INMP441 on the DK's TDM, PORT1 pins). Every layer hid a
bench-only bug the host could never show; the fix chain, in order:

1. **TDM pins must be on port 1** — the peripheral silently no-ops on P2 pads (config/trigger return
   0, the DMA fills zeros). Diagnosed with the new `MICPINS` pad probe; pins now P1.23/P1.14/P1.31
   (the same ones Nordic's own i2s tests use for this DK).
2. **RX overrun self-heal** (`mic_read_block`): a full DMA pool put the driver in ERROR forever;
   PREPARE+START re-arms it without disturbing a healthy stream.
3. **Stack**: the MFCC's scratch (~300 KB at the old caps) lived on the stack → usage fault on the
   first real inference. Now static, caps = the model envelope (~100 KB .bss);
   `CONFIG_MAIN_STACK_SIZE=24576`.
4. **TFLM flag parity**: the Zephyr glue built the lib without `TF_LITE_STATIC_MEMORY` while
   `ai_host.cpp` defined it → `TfLiteTensor` layouts diverged → bus fault. The app CMake now forces
   the define + `-fno-exceptions` on `modules__tflite-micro`.
5. **Performance — 3.6 s → 0.16 s per 1 s window**: float32 MFCC hot path (golden parity holds,
   tol 0.010) + `CONFIG_FPU=y` (Zephyr leaves the M33 FPU OFF by default): **2740 → 119 ms**;
   CMSIS-NN kernels (DK board conf + the CMake trigger): invoke **846 → 37 ms**.
6. **24-block mic pool** (~384 ms): the 64 ms pool overflowed during every inference stall,
   injecting a glitch + a ~90 ms hole into every analysed window.

Serial bench aids added along the way: `MIC` (level + start diagnostics), `MICRAW` (both raw
channels), `MICPINS` (pad toggle counts), and the live log lines
(`mic: level=… top=… mfcc=…ms infer=…ms`, `VOICE: go 78%`, `voice? go 41% (<60%)`).

**Open (bench, next):** a live spoken-word test against the 60 % confidence gate
(`kMinConfidence`, ai_service.cpp), and a suspected TDM start bit-alignment variance (the idle DC
scales by ~2^k between some boots — if real words still miss, auto-realign capture at start until
the DC lands in the lowest band).

## Backend #2 — Nordic nRF Edge AI on the Axon NPU (nRF54LM20B, 2026-07-15)

The nRF54LM20**B** has the **Axon NPU**, and Nordic ships an official Edge AI stack
(`github.com/nrfconnect/sdk-edge-ai`) with a professionally-trained wake-word + 10-keyword
model. For accents/speakers a hand-trained model can't cover, that is the better answer — so on
the B chip the classifier behind `VOICE()` swaps to it.

**Same interface, one compiled:** `src/ai/ai_nrf_edgeai.cpp` is a second implementation of the
`ai_service::` interface (the first is `ai_service.cpp`, MFCC + TFLM). CMake compiles exactly one,
chosen by `CONFIG_TEXTOCHIP_NRF_EDGEAI`; `runtime.cpp`, the `VOICE()`/`INFER` opcodes, and every
BASIC program are unchanged.

**How it plugs in:** Nordic's model is streaming + end-to-end — fed 160 raw `int16` samples (10 ms)
at a time, DSP baked in, a 12-class posterior per block on the NPU. We feed it the **same 16 kHz
mono audio our TDM/INMP441 `aiCapture` already produces** (no PDM mic required), map its keywords
onto ours (`go/left/right/stop` = 3/4/8/9 → 1/2/3/4), and run Nordic's own tuned post-processor
(EMA 0.12, per-class threshold 0.8, 10-in-a-row, 10-block lockout — from `applications/ww_kws`).

**Build (needs the add-on as a module; nothing proprietary is copied into this repo):**
```
west build -b nrf54lm20dk/nrf54lm20b/cpuapp zephyr -p always \
    -- -DEXTRA_ZEPHYR_MODULES=$HOME/projects/sdk-edge-ai
west flash -d build_dk_b -r jlink
```
The board `.conf`/`.overlay` for `nrf54lm20b` enable `&axon`, `NRF_EDGEAI`, `NRF_AXON`, and the
interlayer buffer (6656, per the bundled model). The prebuilt `libnrf_edgeai_cortex-m33.a` + the
compiled model are referenced from the module, not vendored (LicenseRef-Nordic-5-Clause).

**Verified on the real DK (B):** builds/links (FLASH 24.5 % / RAM 32.2 %), boots (`PONG`), the mic
captures, and the **Axon runs inference in 1–12 ms/block** (vs 37 ms CMSIS-NN, 107 ms CPU),
reading `background` when quiet. Open: the live spoken-word test (Nordic's 0.8 threshold wants a
clear word; an input-gain tweak may be needed if the INMP441's level differs from Nordic's DMIC
training). The TFLM/DS-CNN path stays as the ESP32 backend + fallback.

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
  color_detect.c     COLOUR-blob detector: RGB frame -> colour class (host-tested)          ✅
third_party/tflite-micro   the TFLM submodule (built into libtensorflow-microlite.a)       ✅
```

**Two vision paths, one `SEE()` register.** Object CLASSES (person/ball/hand) come from a trained
classifier (`ai_infer_vision`, the TFLM stand-in above). COLOURS (yellow/red/green/blue) come from a
cheap deterministic **colour-blob detector** (`src/ai/color_detect.c`): an RGB frame gives the dominant
saturated colour via HSV hue ranges + a coverage threshold, mapped to the `SEE()` class matching the
product's `VISION_LABELS` (yellow=4, red=5, green=6, blue=7, 0=none). It is the near-term Arducam path
(a request like *"stop at a yellow object"* / *"find the ball"* needs colour, which a classifier does
not give cheaply), and it is **host-tested** (`make test-color`, 9 synthetic frames) the way `features.c`
is the tested core of voice. **Remaining (with the camera):** the Arducam SPI capture HAL
(`hal::camCapture` giving RGB) + a colour vision service that feeds `color_detect` into `vm.setVisionClass`.

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
