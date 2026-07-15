// Background inference service — the firmware half that makes VOICE() live on the
// board. It owns a rolling audio window: each cooperative step it drains new mic
// samples (hal::aiCapture), and once a full analysis window has accumulated it runs
// features.c (MFCC) -> ai_infer and yields the detected class. The runtime drives
// it between VM ticks and writes the result to the VM's class register
// (vm.setAiClass), which INFER / VOICE() reads. Non-blocking by construction
// (drains a bounded chunk per call), so it never stalls the tick loop.
//
// Platform-agnostic: it only calls hal:: + features.c + ai_infer, so the SAME code
// runs on the host (fed by a stub mic) and on the board (fed by the I2S mic).
#ifndef TEXTOCHIP_AI_AI_SERVICE_H
#define TEXTOCHIP_AI_AI_SERVICE_H

namespace ai_service {

// (Re)start the rolling window — called when a running program first wants the
// model (see runtime.cpp, on vm.aiRequested()).
void reset();

// One cooperative step. Drains a chunk of new audio into the window; when the
// window is full, runs MFCC -> ai_infer and slides the window by the hop. Returns
// the freshly detected class index (>= 0) when an inference completed THIS call,
// or -1 when there is nothing new yet. 0 = "none" (quiet / unrecognized).
int poll();

// Bench/tuning view of the latest completed inference: the UN-gated argmax and
// its 0..1 confidence — what the model actually thought even when the confidence
// gate collapsed poll()'s answer to 0. Valid after a poll() that returned >= 0.
void lastTop(int* cls, float* conf);

// Mean-abs level (int16 units) of the last analysed window — 0 means a dead/muted
// mic, speech runs in the thousands. For the serial heartbeat.
int lastLevel();

// Timing (ms) of the last inference: feature extraction vs model invoke.
void lastTiming(int* mfccMs, int* inferMs);

// AGC gain applied to the last analysed window, x10 (10 = no gain). The input
// conditioner scales quiet real-mic speech up into the training loudness band.
int lastGainX10();

// Envelope (max 10 ms block mean-abs, x1000) the AGC keyed on for the last
// window — for bench tuning of the silence gate / target.
int lastEnvX1000();

// Samples the median-3 de-spiker replaced by > 0.1 FS since the previous
// inference — a live read of the physical pin-contact quality.
int lastSpikes();

// The confidence gate as a percentage (for log strings that must not go stale).
int gatePct();

}  // namespace ai_service

#endif  // TEXTOCHIP_AI_AI_SERVICE_H
