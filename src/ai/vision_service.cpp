#include "vision_service.h"

#include "hal.h"

#ifdef TEXTOCHIP_VISION_COLOR
#include "color_detect.h"  // near-term Arducam COLOUR path
#else
#include "ai_vision.h"  // trained OBJECT model (person/ball/…)
#endif

// The background vision service (see vision_service.h). A camera frame is captured
// whole, classified, then a fresh frame begins (no sliding window — each frame is
// independent, unlike the audio window). Static buffer, no heap on the MCU.
//
// Two builds share this one service, chosen at compile time (mirroring how the
// voice tier picks ai_service vs ai_nrf_edgeai):
//   TEXTOCHIP_VISION_COLOR : RGB frame -> tc_detect_color -> a colour class
//                            (VISION_LABELS 4..7). The near-term Arducam path.
//   (default)              : grayscale frame -> ai_infer_vision -> an object class
//                            (VISION_LABELS 1..3). Needs a trained model.
namespace {

#ifdef TEXTOCHIP_VISION_COLOR
constexpr int kBytesPerPixel = 3;  // RGB888
#else
constexpr int kBytesPerPixel = 1;  // grayscale
#endif
// Both current input contracts are 96x96; a different vision model bumps this.
constexpr int kFramePixels = 96 * 96;
constexpr int kFrameBytes = kFramePixels * kBytesPerPixel;
constexpr int kChunk = 1024;  // bytes drained per poll (bounded, non-blocking)

unsigned char g_frame[kFrameBytes];
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
  int room = kFrameBytes - g_have;
  if (room > 0) {
    int want = room < kChunk ? room : kChunk;
#ifdef TEXTOCHIP_VISION_COLOR
    int got = hal::camCaptureRGB(g_frame + g_have, want);
#else
    int got = hal::camCapture(g_frame + g_have, want);
#endif
    g_have += got;
  }

  // 2. Not a full frame yet -> nothing to report.
  if (g_have < kFrameBytes) return -1;

  // 3. Full frame: classify, then start the next frame from scratch.
#ifdef TEXTOCHIP_VISION_COLOR
  int cls = tc_detect_color(g_frame, kFramePixels);
#else
  int cls = ai_infer_vision(g_frame, kFramePixels);
#endif
  g_have = 0;
  return cls < 0 ? 0 : cls;
}
