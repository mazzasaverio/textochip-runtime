// The colour VISION SERVICE end-to-end on the host, NO camera: an RGB frame is
// fed to the camera stub, the background vision_service (built with
// TEXTOCHIP_VISION_COLOR) drains it in bounded chunks like it will on the board,
// runs tc_detect_color on the full frame, and yields the colour class SEE()
// reads. This proves the SERVICE pipeline (chunked capture + assembly + detect),
// one layer above test-color (color_detect alone). Build & run:
//   make test-color-service
#include <cstdio>
#include <cstdlib>
#include <cstdint>

#include "../src/ai/vision_service.h"
#include "hal_host.h"

// Drain a whole frame through the service (many polls of kChunk bytes) and return
// the class it reports, or -1 if it never completed a frame.
static int classify_frame(const uint8_t *rgb, int n_pixels) {
  host_reset_rgb();
  host_feed_rgb(rgb, n_pixels * 3);
  vision_service::reset();
  // 96x96x3 = 27648 bytes at 1024/poll ≈ 27 polls; give it generous headroom.
  for (int i = 0; i < 200; i++) {
    int cls = vision_service::poll();
    if (cls >= 0) return cls;
  }
  return -1;
}

// Drain ONE frame with NO reset: this is what continuous operation looks like,
// and it is where the temporal confirmation lives (reset() clears the history).
static int next_frame(const uint8_t *rgb, int n_pixels) {
  host_reset_rgb();
  host_feed_rgb(rgb, n_pixels * 3);
  for (int i = 0; i < 200; i++) {
    int cls = vision_service::poll();
    if (cls >= 0) return cls;
  }
  return -1;
}

static void fill(uint8_t *b, int n, int r, int g, int bl) {
  for (int i = 0; i < n; i++) {
    b[i * 3] = (uint8_t)r;
    b[i * 3 + 1] = (uint8_t)g;
    b[i * 3 + 2] = (uint8_t)bl;
  }
}

int main(void) {
  // The service's own input size (vision_service.cpp kFramePixels).
  const int n = 96 * 96;
  uint8_t *buf = (uint8_t *)malloc((size_t)n * 3);
  int fail = 0;

  struct {
    const char *name;
    int r, g, b, want;
  } cases[] = {
      {"yellow", 255, 220, 0, 4},
      {"red", 220, 20, 20, 5},
      {"green", 20, 200, 40, 6},
      {"blue", 20, 40, 220, 7},
      {"grey", 128, 128, 128, 0},
  };
  for (auto &c : cases) {
    fill(buf, n, c.r, c.g, c.b);
    int got = classify_frame(buf, n);
    printf("  %-6s frame -> SEE() class %d (want %d)\n", c.name, got, c.want);
    if (got != c.want) fail = 1;
  }

  // ── TEMPORAL CONFIRMATION: one noisy frame must not move the wheels ──
  //
  // The bench failure this pins: every frame's verdict used to reach the VM
  // directly, MOVE is sticky, so a single noisy frame lurched the robot. The
  // reported class now changes only when two consecutive frames agree.
  {
    uint8_t *y = (uint8_t *)malloc((size_t)n * 3);
    uint8_t *g = (uint8_t *)malloc((size_t)n * 3);
    fill(y, n, 255, 220, 0);  // yellow
    fill(g, n, 128, 128, 128);  // grey = nothing

    int a = classify_frame(y, n);      // first frame after reset: counts at once
    int b = next_frame(g, n);          // ONE noisy "nothing" frame
    int c = next_frame(y, n);          // yellow again
    int d = next_frame(g, n);          // nothing...
    int e = next_frame(g, n);          // ...twice in a row: NOW it changes
    printf("  flicker: %d %d %d %d %d (want 4 4 4 4 0)\n", a, b, c, d, e);
    if (a != 4 || b != 4 || c != 4 || d != 4 || e != 0) fail = 1;

    free(y);
    free(g);
  }

  free(buf);
  if (!fail)
    printf(
        "OK: RGB frame -> camCaptureRGB stub -> vision_service (colour) -> "
        "tc_detect_color -> SEE() class\n");
  else
    printf("FAIL: wrong SEE() class from the colour vision service\n");
  return fail;
}
