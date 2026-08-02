/*
 * SEE() person detection on the Axon NPU (nRF54LM20B) — Nordic's pretrained
 * person-detection model, fed by our own Arducam driver.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The model, its generated header and the box decoder (postprocessing.c) come
 * from Nordic's nRF Edge AI add-on (applications/person_detection,
 * LicenseRef-Nordic-5-Clause) and are REFERENCED from that module at build
 * time, never copied into this repo — the same arrangement as the ww_kws voice
 * model. The input-preparation math below (LUT quantization, grey padding)
 * follows that sample's main.c; it is a direct consequence of the model's
 * published input contract (quant_mult/quant_round/quant_zp).
 *
 * Flow per frame: 128x128 RGB565 chunks from the camera -> quantized planar
 * int8 into a 160x128 input (centred, grey side padding) -> one synchronous
 * Axon inference (~ms) -> box decode + NMS -> the best box becomes
 * SEE()/SEEX()/SEESIZE().
 */
#include "../../src/ai/vision_npu.h"

#if defined(TEXTOCHIP_VISION_NPU)

#include <string.h>

#include <axon/nrf_axon_platform.h>
#include <drivers/axon/nrf_axon_driver.h>
#include <drivers/axon/nrf_axon_nn_infer.h>

#include "generated/nrf_axon_model_person_det_.h"
extern "C" {
#include "postprocessing.h"
}

#include "arducam_mega.h"

namespace {

constexpr int kCamW = 128, kCamH = 128;
constexpr int kModelW = 160, kModelH = 128;
constexpr int kPadLeft = (kModelW - kCamW) / 2;  // grey side bars, like Nordic's app
constexpr int kCamPixels = kCamW * kCamH;

int8_t g_input[kModelW * kModelH * 3];
int8_t g_output[NRF_AXON_MODEL_PERSON_DET_PACKED_OUTPUT_SIZE];
int8_t g_lutRB[32];
int8_t g_lutG[64];
uint8_t g_chunk[1024];  // raw RGB565 drained per poll (even = whole pixels)

enum class St { kUninit, kFailed, kRun };
St g_state = St::kUninit;
int g_pixels = 0;  // camera pixels already converted into g_input
int g_x = 0, g_size = 0, g_score = 0;

int8_t quantize(float value, const nrf_axon_nn_compiled_model_input_s* in) {
  const float scale = (float)in->quant_mult / (float)(1u << in->quant_round);
  int32_t q = (int32_t)(value * scale) + in->quant_zp;
  if (q > 127) q = 127;
  if (q < -128) q = -128;
  return (int8_t)q;
}

bool init() {
  if (nrf_axon_platform_init() != NRF_AXON_RESULT_SUCCESS) return false;
  if (nrf_axon_nn_model_validate(&model_person_det) != NRF_AXON_RESULT_SUCCESS)
    return false;
  const nrf_axon_nn_compiled_model_input_s* in =
      nrf_axon_nn_model_1st_external_input(&model_person_det);
  // Grey padding everywhere first (the side bars keep it for good), then the
  // two 5/6-bit -> symmetric-float -> int8 lookup tables.
  memset(g_input, quantize(0.0f, in), sizeof(g_input));
  for (int i = 0; i < 32; i++)
    g_lutRB[i] = quantize(((float)i / 32.0f) * 2.0f - 1.0f, in);
  for (int i = 0; i < 64; i++)
    g_lutG[i] = quantize(((float)i / 64.0f) * 2.0f - 1.0f, in);
  decode_init(&model_person_det);
  return true;
}

}  // namespace

int npu_vision_poll(void) {
  if (g_state == St::kFailed) return 0;
  if (g_state == St::kUninit) {
    if (!init()) {
      g_state = St::kFailed;  // no NPU / bad model: SEE() reads "nothing"
      return 0;
    }
    g_state = St::kRun;
  }

  int got = arducam_capture_raw565(g_chunk, sizeof(g_chunk));
  if (got <= 0) return -1;

  const int px = got / 2;
  for (int p = 0; p < px && g_pixels + p < kCamPixels; p++) {
    const int idx = g_pixels + p;
    const int row = idx / kCamW, col = idx % kCamW;
    const int dst = row * kModelW + (col + kPadLeft);
    const uint16_t v =
        (uint16_t)((uint16_t)g_chunk[p * 2] << 8 | g_chunk[p * 2 + 1]);
    g_input[0 * kModelW * kModelH + dst] = g_lutRB[(v >> 11) & 0x1F];
    g_input[1 * kModelW * kModelH + dst] = g_lutG[(v >> 5) & 0x3F];
    g_input[2 * kModelW * kModelH + dst] = g_lutRB[v & 0x1F];
  }
  g_pixels += px;
  if (g_pixels < kCamPixels) return -1;
  g_pixels = 0;

  if (nrf_axon_nn_model_infer_sync(&model_person_det, g_input, g_output) !=
      NRF_AXON_RESULT_SUCCESS) {
    g_x = 0;
    g_size = 0;
    g_score = 0;
    return 0;
  }

  struct detection_box boxes[8];
  const size_t n = decode_output(&model_person_det, g_output, boxes, 8);
  if (n == 0) {
    g_x = 0;
    g_size = 0;
    g_score = 0;
    return 0;
  }
  // The strongest box (decode sorts by score before NMS keeps the survivors).
  const struct detection_box* b = &boxes[0];
  float cx = (b->x1 + b->x2) * 0.5f - (float)kPadLeft;  // model -> camera space
  if (cx < 0) cx = 0;
  if (cx > kCamW) cx = kCamW;
  g_x = (int)(cx * 100.0f / (float)kCamW);
  float area = (b->x2 - b->x1) * (b->y2 - b->y1);
  int size = (int)(area * 100.0f / (float)(kCamW * kCamH));
  if (size < 1) size = 1;
  if (size > 100) size = 100;
  g_size = size;
  g_score = (int)(b->score * 1000.0f);
  if (g_score < 0) g_score = 0;
  if (g_score > 1000) g_score = 1000;
  return 1;  // person — VISION_LABELS class 1
}

int npu_vision_x(void) { return g_x; }
int npu_vision_score(void) { return g_score; }
int npu_vision_size(void) { return g_size; }

#endif  // TEXTOCHIP_VISION_NPU
