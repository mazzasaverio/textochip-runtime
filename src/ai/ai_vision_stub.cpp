// Fallback VISION backend — NO model linked. ai_infer_vision returns 0 ("nothing"),
// so a build without a camera model still compiles and runs the capture -> service
// path; SEE() simply always reads "nothing". The host vision test links
// ai_vision_tflm.cpp (real person-detection); the board links this stub until the
// camera + a vision model are brought up. Exactly one ai_vision_* backend per build.
#include "ai_vision.h"

extern "C" int ai_infer_vision(const unsigned char* /*image*/, int /*n_pixels*/) {
  return 0;
}

extern "C" int ai_vision_num_classes(void) { return 1; }  // just "nothing"
