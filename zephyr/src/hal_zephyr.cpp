// Zephyr implementation of the HAL — builds with the nRF Connect SDK (nRF54L) and
// upstream Zephyr (ESP32-S3). LEDs / button / serial / time work from the board's
// standard devicetree; the buzzer (PWM) needs a pwm node in app.overlay (see README).
//
// NOTE: this file is NOT compiled on the dev machine (no Zephyr toolchain present).
// It is a correct-shaped starting point — expect to tune prj.conf / the overlay on
// the first `west build`. It is the ONLY file that differs per board.
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#if DT_NODE_EXISTS(DT_NODELABEL(pwm0))
#include <zephyr/drivers/pwm.h>
#endif

#include "hal.h"

// ── Serial: the chosen console UART is the link to the IDE (Web Serial) ──
static const struct device* const uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

// ── GPIO: the bytecode carries RAW pin numbers (baked from lib/boardProfile.ts).
// Convention: raw = port*32 + pin  → gpio0 for 0..31, gpio1 for 32.. (e.g. nRF P1).
static const struct device* gpio_port(int pin) {
#if DT_NODE_EXISTS(DT_NODELABEL(gpio1))
  if (pin >= 32) return DEVICE_DT_GET(DT_NODELABEL(gpio1));
#endif
  return DEVICE_DT_GET(DT_NODELABEL(gpio0));
}
static inline gpio_pin_t gpio_index(int pin) { return (gpio_pin_t)(pin % 32); }

namespace hal {

void init() { /* console + gpio controllers are ready at boot */ }

void pinMode(int pin, bool output) {
  gpio_pin_configure(gpio_port(pin), gpio_index(pin),
                     output ? GPIO_OUTPUT_INACTIVE : (GPIO_INPUT | GPIO_PULL_UP));
}
void pinWrite(int pin, int level) { gpio_pin_set(gpio_port(pin), gpio_index(pin), level); }
int pinRead(int pin) { return gpio_pin_get(gpio_port(pin), gpio_index(pin)); }

#if DT_NODE_EXISTS(DT_NODELABEL(pwm0))
static const struct device* const pwm_dev = DEVICE_DT_GET(DT_NODELABEL(pwm0));
void tone(int /*pin*/, int hz) {
  if (hz <= 0) return;
  uint32_t period = 1000000000u / (uint32_t)hz;  // ns
  // TODO(board): map `pin` -> its PWM channel via the overlay; channel 0 assumed.
  pwm_set(pwm_dev, 0, period, period / 2, 0);
}
// Silence = a valid period with a 0 pulse width (constant LOW). A period of 0
// is rejected by the LEDC driver, which would leave the channel running its
// boot-default duty → a continuous tone. Keep a real period, drop the duty.
void toneOff(int /*pin*/) { pwm_set(pwm_dev, 0, 1000000u, 0, 0); }
#else
// No pwm node yet → buzzer is a no-op (LEDs/button still work). Add one in app.overlay.
void tone(int, int) {}
void toneOff(int) {}
#endif

uint32_t nowMs() { return (uint32_t)k_uptime_get(); }

int serialReadChar() {
  unsigned char c;
  return uart_poll_in(uart_dev, &c) == 0 ? (int)c : -1;
}
static void put_str(const char* s) {
  for (; *s; ++s) uart_poll_out(uart_dev, (unsigned char)*s);
}
void serialWrite(const std::string& s) { put_str(s.c_str()); }
void serialWriteLine(const std::string& s) {
  put_str(s.c_str());
  uart_poll_out(uart_dev, '\n');
}

}  // namespace hal
