#include "vision_service.h"

#include "ai_vision.h"
#include "hal.h"

// The background vision service (see vision_service.h). A camera frame is captured
// whole, classified, then a fresh frame begins (no sliding window — each frame is
// independent, unlike the audio window). Static buffer, no heap on the MCU.
namespace {

// The person-detection model's input: 96x96 grayscale = 9216 bytes. A different vision
// model bumps this (and its own input contract).
constexpr int kFramePixels = 96 * 96;
constexpr int kChunk = 1024;  // bytes drained per poll (bounded, non-blocking)

unsigned char g_frame[kFramePixels];
int g_have = 0;
bool g_ready = false;

}  // namespace

void vision_service::reset() {
  g_have = 0;
  g_ready = true;
}

int vision_service::poll() {
  if (!g_ready) reset();

  // 1. Drain a bounded chunk of camera bytes into the frame (non-blocking).
  int room = kFramePixels - g_have;
  if (room > 0) {
    int want = room < kChunk ? room : kChunk;
    int got = hal::camCapture(g_frame + g_have, want);
    g_have += got;
  }

  // 2. Not a full frame yet -> nothing to report.
  if (g_have < kFramePixels) return -1;

  // 3. Full frame: classify, then start the next frame from scratch.
  int cls = ai_infer_vision(g_frame, kFramePixels);
  g_have = 0;
  return cls < 0 ? 0 : cls;
}
