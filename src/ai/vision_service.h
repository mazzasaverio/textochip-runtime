// Background VISION inference service — the firmware half that makes SEE() live on the
// board. Each cooperative step it grabs camera bytes (hal::camCapture); when a full
// frame has been captured it runs ai_infer_vision over it and yields the detected
// class. The runtime drives it between VM ticks and writes the result to the VM's
// vision register (vm.setVisionClass), which INFER vision / SEE() reads. Non-blocking
// by construction (a bounded drain per call).
//
// Platform-agnostic: it only calls hal:: + ai_infer_vision, so the SAME code runs on
// the host (fed by a stub camera) and on the board (the DVP camera). Mirrors
// ai_service (the voice service), one sense apart.
#ifndef TEXTOCHIP_AI_VISION_SERVICE_H
#define TEXTOCHIP_AI_VISION_SERVICE_H

namespace vision_service {

// (Re)start frame accumulation — called when a running program first wants the model.
void reset();

// One cooperative step. Drains camera bytes into the frame buffer; when a full frame
// is captured, runs ai_infer_vision. Returns the class (>= 0) when an inference
// completed THIS call, or -1 when there is no full frame yet. 0 = "nothing".
int poll();

}  // namespace vision_service

#endif  // TEXTOCHIP_AI_VISION_SERVICE_H
