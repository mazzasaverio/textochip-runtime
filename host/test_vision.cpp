// Edge-AI VISION end-to-end (firmware side): a real 96x96 grayscale image -> the hal
// camera stub -> vision_service -> ai_infer_vision (TFLM person-detection) -> class,
// then the firmware VM branching on SEE(). Uses the person-detection model + its
// held-out test images (bundled in the TFLM submodule) — a Phase-0 vision stand-in,
// like micro_speech was for voice. No camera. Build & run:  make test-vision
#include <cstdint>
#include <cstdio>

#include "../src/ai/ai_vision.h"
#include "../src/ai/vision_service.h"
#include "../src/isa.h"
#include "../src/vm.h"
#include "hal_host.h"
#include "tensorflow/lite/micro/examples/person_detection/model_settings.h"
#include "tensorflow/lite/micro/examples/person_detection/testdata/no_person_image_data.h"
#include "tensorflow/lite/micro/examples/person_detection/testdata/person_image_data.h"

// The bundled image bytes ARE the model's int8 input (the example memcpy's them
// straight into the int8 tensor). Recover the grayscale a real camera would give
// (uint8 = int8 + 128); ai_infer_vision converts it back (-128) — a lossless
// round-trip, so the model sees the exact int8 image.
static int classify_via_service(const unsigned char* img, int n) {
  static unsigned char frame[96 * 96];
  if (n > 96 * 96) n = 96 * 96;
  for (int i = 0; i < n; i++) {
    frame[i] = (unsigned char)((int)(signed char)img[i] + 128);
  }
  host_reset_image();
  host_feed_image(frame, n);
  vision_service::reset();
  int cls = -1;
  for (int i = 0; i < 200 && cls < 0; i++) cls = vision_service::poll();
  return cls;
}

int main(void) {
  int fail = 0;

  // 1) The service classifies real images (camera stub -> service -> TFLM).
  printf("vision: camera stub -> vision_service -> ai_infer_vision (TFLM)\n");
  int p = classify_via_service(g_person_image_data, g_person_image_data_size);
  int np = classify_via_service(g_no_person_image_data, g_no_person_image_data_size);
  printf("  person image    -> %d (want %d = person)%s\n", p, kPersonIndex,
         p == kPersonIndex ? "" : "   <-- MISMATCH");
  printf("  no-person image -> %d (want %d = nothing)%s\n", np, kNotAPersonIndex,
         np == kNotAPersonIndex ? "" : "   <-- MISMATCH");
  if (p != kPersonIndex) fail = 1;
  if (np != kNotAPersonIndex) fail = 1;

  // 2) Plumbing: a SEE() program reads the VM's vision register (separate from voice).
  //    0 INFER vision  1 PUSH 1  2 EQ  3 JZ 5  4 SET 1 1  5 HALT  -> pin1 iff SEE()==1
  const char* prog[] = {"INFER vision", "PUSH 1", "EQ",
                        "JZ 5",         "SET 1 1", "HALT"};
  VM vm;
  vm.reset();
  for (const char* line : prog) {
    Instruction in;
    if (parseInstructionLine(line, in)) vm.addInstruction(in);
  }
  vm.setVisionClass(1);  // the service injects "saw a person"
  vm.start();
  for (int i = 0; i < 20 && vm.getState() != VM_STOPPED; i++) vm.tick();
  int pin1 = host_get_level(1);
  printf("  SEE()=person -> pin1=%d (want 1)%s\n", pin1,
         pin1 == 1 ? "" : "   <-- MISMATCH");
  if (pin1 != 1) fail = 1;

  if (!fail) {
    printf(
        "OK: vision — image -> vision_service -> TFLM -> class -> SEE() branches\n");
  }
  return fail;
}
