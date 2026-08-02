// Host (PC) implementation of the HAL — lets the VM run with no board at all.
// Pins are a simulated array, the clock is advanced from main(), serial = stdout.
// This is the spirit of Zephyr's native_sim, but with the plain host compiler so
// it builds & runs without installing the Zephyr toolchain.
#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "hal.h"
#include "hal_host.h"

namespace {
std::array<int, 128> g_level{};   // last written pin levels
std::array<int, 128> g_mode{};    // 0=INPUT_PULLUP, 1=OUTPUT, 2=INPUT_PULLDOWN
int g_distance = 400;             // simulated ultrasonic distance in cm (DIST)
uint32_t g_now = 0;

// Edge-AI mic stub: a queue of PCM samples a test feeds via host_feed_audio();
// hal::aiCapture drains it (stand-in for the board's I2S mic). Plus the last MOVE
// wheel speeds, so a test can assert the robot drove/stopped.
std::vector<int16_t> g_audio;
size_t g_audioPos = 0;
std::vector<unsigned char> g_image;  // grayscale camera frame stub (host_feed_image)
size_t g_imagePos = 0;
std::vector<unsigned char> g_rgb;  // RGB camera frame stub (host_feed_rgb)
size_t g_rgbPos = 0;
int g_moveL = 0, g_moveR = 0;
bool g_moved = false;
}  // namespace

void host_advance(uint32_t ms) { g_now += ms; }
void host_set_distance(int cm) { g_distance = cm; }
int host_get_level(int pin) { return (pin >= 0 && pin < 128) ? g_level[pin] : 0; }

// Edge-AI mic stub controls (tests): queue PCM for hal::aiCapture, clear it, and
// read back the last MOVE the program issued.
void host_feed_audio(const int16_t* samples, int n) {
  g_audio.insert(g_audio.end(), samples, samples + n);
}
void host_reset_audio() {
  g_audio.clear();
  g_audioPos = 0;
}
bool host_audio_drained() { return g_audioPos >= g_audio.size(); }
void host_feed_image(const unsigned char* pixels, int n) {
  g_image.insert(g_image.end(), pixels, pixels + n);
}
void host_reset_image() {
  g_image.clear();
  g_imagePos = 0;
}
void host_feed_rgb(const unsigned char* pixels, int n) {
  g_rgb.insert(g_rgb.end(), pixels, pixels + n);
}
void host_reset_rgb() {
  g_rgb.clear();
  g_rgbPos = 0;
}
bool host_get_move(int* left, int* right) {
  if (left != nullptr) *left = g_moveL;
  if (right != nullptr) *right = g_moveR;
  return g_moved;
}

namespace hal {

void init() {}

void pinMode(int pin, int mode) {
  if (pin >= 0 && pin < 128) g_mode[pin] = mode;
}

void pinWrite(int pin, int level) {
  if (pin < 0 || pin >= 128) return;
  if (g_level[pin] != level) {  // only print transitions, keeps the trace readable
    g_level[pin] = level;
    std::printf("    [t=%6ums] pin%-2d = %d\n", g_now, pin, level);
  }
}

int pinRead(int pin) {
  if (pin >= 0 && pin < 128) {
    if (g_mode[pin] == 0) return 1;  // INPUT_PULLUP idle reads HIGH
    if (g_mode[pin] == 2) return 0;  // INPUT_PULLDOWN idle reads LOW
    return g_level[pin];             // OUTPUT: the last written value
  }
  return 0;
}

// No ADC on a PC — the host always reads 0 (a program's AREAD path still runs).
int analogRead(int /*pin*/) { return 0; }

int distanceCm() { return g_distance; }

void tone(int pin, int hz) { std::printf("    [t=%6ums] buzzer pin%d -> %d Hz\n", g_now, pin, hz); }
void toneOff(int pin) { std::printf("    [t=%6ums] buzzer pin%d off\n", g_now, pin); }

void servo(int pin, int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  std::printf("    [t=%6ums] servo pin%d -> %d deg\n", g_now, pin, angle);
}

// Differential-drive motors — host edition just prints the wheel speeds (no real
// motors on a PC). The in-browser simulator drives a robot from MOVE; the board
// build (hal_zephyr.cpp) drives the L298N.
bool isMoving() { return g_moveL != 0 || g_moveR != 0; }

void move(int left, int right) {
  auto clamp = [](int v) { return v < -255 ? -255 : v > 255 ? 255 : v; };
  int l = clamp(left), r = clamp(right);
  if (!g_moved || l != g_moveL || r != g_moveR) {  // print transitions only
    std::printf("    [t=%6ums] move  L=%-4d R=%-4d\n", g_now, l, r);
  }
  g_moveL = l;
  g_moveR = r;
  g_moved = true;
}

// Edge-AI mic stub: drain up to `n` queued PCM samples (host_feed_audio) into
// `out`. Stands in for the board's I2S mic (hal_zephyr.cpp) so the whole
// capture -> features -> ai_infer service runs on the PC with no hardware.
int aiCapture(int16_t* out, int n) {
  int avail = (int)(g_audio.size() - g_audioPos);
  int k = avail < n ? avail : n;
  for (int i = 0; i < k; i++) out[i] = g_audio[g_audioPos++];
  return k;
}

// The host has no real mic (aiCapture is fed by tests), so there is nothing to
// diagnose — a fixed marker keeps the MIC command's format uniform across builds.
std::string micStatus() { return "host"; }

// No real I2S block on the host, so there are no raw L/R words to inspect.
int aiCaptureRaw(int32_t* out, int n) {
  (void)out;
  (void)n;
  return 0;
}

// No pads to probe on the host.
std::string micPinsProbe() { return "n/a"; }
std::string camProbe() { return "n/a (host build has no camera)"; }
std::string camPinsProbe() { return "n/a (host build has no camera)"; }
std::string camSetWB(int) { return "n/a (host build has no camera)"; }
std::string camBitbangProbe() { return "n/a (host build has no camera)"; }
std::string railProbe() { return "n/a (host build has no board)"; }
std::string padScan() { return "n/a (host build has no board)"; }

// Edge-AI camera stub: drain up to `max` queued grayscale bytes (host_feed_image)
// into `out`. Stands in for the board's DVP camera (hal_zephyr.cpp) so the whole
// capture -> vision_service -> ai_infer_vision path runs on the PC with no hardware.
int camCapture(unsigned char* out, int max) {
  int avail = (int)(g_image.size() - g_imagePos);
  int k = avail < max ? avail : max;
  for (int i = 0; i < k; i++) out[i] = g_image[g_imagePos++];
  return k;
}

int camCaptureRGB(unsigned char* out, int max) {
  int avail = (int)(g_rgb.size() - g_rgbPos);
  int k = avail < max ? avail : max;
  for (int i = 0; i < k; i++) out[i] = g_rgb[g_rgbPos++];
  return k;
}

// Non-volatile storage, host edition: a plain file in the working directory.
// It persists across separate runs of the binary, so a test can SAVE in one
// process and observe autorun in the next — a faithful stand-in for flash +
// power-cycle (the Zephyr HAL uses NVS on the board's storage partition).
static const char* kStorePath = ".textochip_store";

bool storeSave(const std::string& program) {
  std::FILE* f = std::fopen(kStorePath, "wb");
  if (f == nullptr) return false;
  size_t n = std::fwrite(program.data(), 1, program.size(), f);
  std::fclose(f);
  return n == program.size();
}

bool storeLoad(std::string& out) {
  std::FILE* f = std::fopen(kStorePath, "rb");
  if (f == nullptr) return false;  // nothing saved
  out.clear();
  char buf[512];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  std::fclose(f);
  return !out.empty();
}

void storeClear() { std::remove(kStorePath); }

void storeStatus(bool* mounted, int* savedBytes, int* sectorSize, int* sectorCount) {
  if (mounted) *mounted = true;  // the host store is always "mounted" (a file)
  if (sectorSize) *sectorSize = -1;
  if (sectorCount) *sectorCount = -1;
  if (savedBytes) {
    std::string s;
    *savedBytes = storeLoad(s) ? (int)s.size() : -1;
  }
}

uint32_t nowMs() { return g_now; }

int serialReadChar() { return -1; }  // host demo injects lines via runtime::feedLine
void serialWrite(const std::string& s) { std::printf("%s", s.c_str()); }
void serialWriteLine(const std::string& s) { std::printf("%s\n", s.c_str()); }

}  // namespace hal
