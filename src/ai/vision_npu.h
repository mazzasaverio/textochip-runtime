// SEE() person detection on the Axon NPU — Nordic's pretrained model, the same
// shortcut the voice tier took with ww_kws: prove the path with a professionally
// trained model before training our own. Implemented only in the Zephyr NPU
// build (zephyr/src/ai_vision_npu.cpp); the host and colour builds never
// compile it.
#ifndef TEXTOCHIP_AI_VISION_NPU_H
#define TEXTOCHIP_AI_VISION_NPU_H

#ifdef __cplusplus
extern "C" {
#endif

// One cooperative step: drain camera bytes, convert into the model input, and
// when a frame is complete run inference on the Axon. Returns -1 while the
// frame is still filling, else the class (0 = nothing, 1 = person — matching
// VISION_LABELS in the product, where SEE()="person" is class 1).
int npu_vision_poll(void);

// Best box of the last completed inference: centre 0..100 across the camera's
// view, and its area as 0..100 of the frame. Both 0 when nothing was detected.
int npu_vision_x(void);
int npu_vision_size(void);

#ifdef __cplusplus
}
#endif

#endif  // TEXTOCHIP_AI_VISION_NPU_H
