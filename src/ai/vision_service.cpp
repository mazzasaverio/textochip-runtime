#include "vision_service.h"

#include "hal.h"

#if defined(TEXTOCHIP_VISION_NPU)
#include "vision_npu.h"  // Nordic person detection on the Axon (zephyr-only impl)
#elif defined(TEXTOCHIP_VISION_COLOR)
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

#ifndef TEXTOCHIP_VISION_NPU
// The NPU backend captures and converts on its own; this frame buffer exists
// only for the colour / object-model paths (27 KB the NPU build spends on the
// model instead).
#ifdef TEXTOCHIP_VISION_COLOR
constexpr int kBytesPerPixel = 3;  // RGB888
#else
constexpr int kBytesPerPixel = 1;  // grayscale
#endif
// Both current input contracts are 96x96; a different vision model bumps this.
constexpr int kFrameWidth = 96;
constexpr int kFrameHeight = 96;
constexpr int kFramePixels = kFrameWidth * kFrameHeight;
constexpr int kFrameBytes = kFramePixels * kBytesPerPixel;
constexpr int kChunk = 1024;  // bytes drained per poll (bounded, non-blocking)

unsigned char g_frame[kFrameBytes];
int g_have = 0;
#endif
bool g_ready = false;
int g_x = 0;     // last blob centroid, 0..100 (SEEX())
int g_size = 0;  // last blob coverage, 0..100 (SEESIZE())

// TEMPORAL CONFIRMATION — the piece the voice tier always had and vision lacked.
//
// Every frame's verdict used to go straight to the VM, and MOVE is sticky: one
// noisy frame flipped the class, the wheels fired, and the robot lurched. The
// voice path never had this problem because its post-processor smooths and
// debounces before anything fires. Same medicine here, sized for a slow frame
// rate: the REPORTED class changes only when two consecutive frames agree. The
// first frame after reset() counts immediately (a program that just started
// deserves an answer), and a single-frame flicker — to another colour or to
// nothing — changes nothing.
int g_confirmed = 0;  // what SEE() actually reads
int g_lastRaw = -1;   // previous frame's raw verdict; -1 = no history yet
bool g_emaValid = false;  // geometry EMA is seeded (see applyGeometry)

// SMOOTHED geometry — the tracking half of what the temporal confirmation does
// for the class. A detector's box jitters a few percent frame to frame, MOVE is
// sticky, and a robot steering on raw jitter weaves. Standard practice is an
// exponential moving average on position and size: the first sample after a
// target appears is taken as-is (no ramp-up from zero), later ones blend 3:1.
void applyGeometry(int raw, int nx, int nsize) {
  if (raw == g_confirmed && g_confirmed != 0) {
    if (!g_emaValid) {
      g_x = nx;
      g_size = nsize;
      g_emaValid = true;
    } else {
      g_x = (g_x * 3 + nx + 2) / 4;
      g_size = (g_size * 3 + nsize + 2) / 4;
    }
  }
  if (g_confirmed == 0) {
    g_x = 0;
    g_size = 0;
    g_emaValid = false;
  }
}

}  // namespace

void vision_service::reset() {
#ifndef TEXTOCHIP_VISION_NPU
  g_have = 0;
#endif
  g_emaValid = false;
  g_x = 0;
  g_size = 0;
  g_confirmed = 0;
  g_lastRaw = -1;
  g_ready = true;
}

int vision_service::poll() {
  if (!g_ready) reset();

#ifdef TEXTOCHIP_VISION_NPU
  // The NPU backend owns its own capture + conversion + inference; this service
  // adds what every vision backend needs: the temporal confirmation.
  const int raw = npu_vision_poll();
  if (raw < 0) return -1;
  if (g_lastRaw < 0 || raw == g_lastRaw) g_confirmed = raw;
  g_lastRaw = raw;
  applyGeometry(raw, npu_vision_x(), npu_vision_size());
  return g_confirmed;
#else
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
  tc_color_blob blob = tc_detect_color_blob(g_frame, kFrameWidth, kFrameHeight);
  const int raw = blob.cls;
#else
  const int inferred = ai_infer_vision(g_frame, kFramePixels);
  const int raw = inferred < 0 ? 0 : inferred;
#endif
  g_have = 0;

  // Confirm across frames (see the note on g_confirmed above).
  if (g_lastRaw < 0 || raw == g_lastRaw) g_confirmed = raw;
  g_lastRaw = raw;

#if defined(TEXTOCHIP_VISION_COLOR)
  // Geometry follows the CONFIRMED class — smoothed, held through a one-frame
  // flicker, zeroed when "nothing" is confirmed (applyGeometry).
  applyGeometry(raw, blob.x, blob.size);
#else
  g_x = 0;
  g_size = 0;
#endif
  return g_confirmed;
#endif  // TEXTOCHIP_VISION_NPU
}

void vision_service::lastStats(long* too_dark, long* too_grey, long* no_band,
                               long* counted) {
#if defined(TEXTOCHIP_VISION_COLOR) && !defined(TEXTOCHIP_VISION_NPU)
  tc_color_stats(g_frame, kFrameWidth, kFrameHeight, too_dark, too_grey, no_band,
                 counted);
#else
  if (too_dark) *too_dark = 0;
  if (too_grey) *too_grey = 0;
  if (no_band) *no_band = 0;
  if (counted) *counted = 0;
#endif
}

void vision_service::lastHueHist(long* hist12) {
#if defined(TEXTOCHIP_VISION_COLOR) && !defined(TEXTOCHIP_VISION_NPU)
  tc_color_hist(g_frame, kFrameWidth, kFrameHeight, hist12);
#else
  if (hist12)
    for (int i = 0; i < 12; i++) hist12[i] = 0;
#endif
}

const unsigned char* vision_service::frameData(int* bytes, int* width, int* height) {
#if defined(TEXTOCHIP_VISION_COLOR) && !defined(TEXTOCHIP_VISION_NPU)
  if (bytes) *bytes = kFrameBytes;
  if (width) *width = kFrameWidth;
  if (height) *height = kFrameHeight;
  return g_frame;
#else
  if (bytes) *bytes = 0;
  if (width) *width = 0;
  if (height) *height = 0;
  return nullptr;
#endif
}

void vision_service::frameDims(int* w, int* h) {
#if defined(TEXTOCHIP_VISION_NPU)
  npu_vision_dims(w, h);
#elif defined(TEXTOCHIP_VISION_COLOR)
  if (w) *w = kFrameWidth;
  if (h) *h = kFrameHeight;
#else
  if (w) *w = 0;
  if (h) *h = 0;
#endif
}

int vision_service::frameRow(int y, unsigned char* rgb888, int maxBytes) {
#if defined(TEXTOCHIP_VISION_NPU)
  return npu_vision_row(y, rgb888, maxBytes);
#elif defined(TEXTOCHIP_VISION_COLOR)
  if (!rgb888 || y < 0 || y >= kFrameHeight || maxBytes < kFrameWidth * 3)
    return 0;
  for (int i = 0; i < kFrameWidth * 3; i++)
    rgb888[i] = g_frame[y * kFrameWidth * 3 + i];
  return kFrameWidth * 3;
#else
  (void)y;
  (void)rgb888;
  (void)maxBytes;
  return 0;
#endif
}

int vision_service::lastScore() {
#ifdef TEXTOCHIP_VISION_NPU
  return npu_vision_score();
#else
  return 0;
#endif
}

int vision_service::lastX() { return g_x; }
int vision_service::lastSize() { return g_size; }
