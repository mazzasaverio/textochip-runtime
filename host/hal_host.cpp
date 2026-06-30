// Host (PC) implementation of the HAL — lets the VM run with no board at all.
// Pins are a simulated array, the clock is advanced from main(), serial = stdout.
// This is the spirit of Zephyr's native_sim, but with the plain host compiler so
// it builds & runs without installing the Zephyr toolchain.
#include <array>
#include <cstdio>
#include <string>

#include "hal.h"
#include "hal_host.h"

namespace {
std::array<int, 128> g_level{};   // last written pin levels
std::array<int, 128> g_mode{};    // 0=INPUT_PULLUP, 1=OUTPUT, 2=INPUT_PULLDOWN
std::array<int, 128> g_servo{};   // last servo angle per pin
std::array<int, 128> g_analog{};  // simulated ADC reading per pin
uint32_t g_now = 0;
int g_buttonPin = -1;
bool g_buttonPressed = false;
}  // namespace

void host_advance(uint32_t ms) { g_now += ms; }
void host_set_button(int pin, bool pressed) {
  g_buttonPin = pin;
  g_buttonPressed = pressed;
}
void host_set_analog(int pin, int value) {
  if (pin >= 0 && pin < 128) g_analog[pin] = value;
}
int host_get_level(int pin) { return (pin >= 0 && pin < 128) ? g_level[pin] : 0; }

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
  if (pin == g_buttonPin) return g_buttonPressed ? 0 : 1;  // active-low: pressed = LOW
  if (pin >= 0 && pin < 128) {
    if (g_mode[pin] == 0) return 1;  // INPUT_PULLUP idle reads HIGH
    if (g_mode[pin] == 2) return 0;  // INPUT_PULLDOWN idle reads LOW
    return g_level[pin];             // OUTPUT: the last written value
  }
  return 0;
}

int analogRead(int pin) { return (pin >= 0 && pin < 128) ? g_analog[pin] : 0; }

void tone(int pin, int hz) { std::printf("    [t=%6ums] buzzer pin%d -> %d Hz\n", g_now, pin, hz); }
void toneOff(int pin) { std::printf("    [t=%6ums] buzzer pin%d off\n", g_now, pin); }

void servo(int pin, int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  if (pin >= 0 && pin < 128) g_servo[pin] = angle;
  std::printf("    [t=%6ums] servo pin%d -> %d deg\n", g_now, pin, angle);
}

// Differential-drive motors — host edition just prints the wheel speeds (no real
// motors on a PC). The in-browser simulator drives a robot from MOVE; the board
// build (hal_zephyr.cpp) drives the L298N.
void move(int left, int right) {
  auto clamp = [](int v) { return v < -255 ? -255 : v > 255 ? 255 : v; };
  std::printf("    [t=%6ums] move  L=%-4d R=%-4d\n", g_now, clamp(left),
              clamp(right));
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

uint32_t nowMs() { return g_now; }

int serialReadChar() { return -1; }  // host demo injects lines via runtime::feedLine
void serialWrite(const std::string& s) { std::printf("%s", s.c_str()); }
void serialWriteLine(const std::string& s) { std::printf("%s\n", s.c_str()); }

}  // namespace hal
