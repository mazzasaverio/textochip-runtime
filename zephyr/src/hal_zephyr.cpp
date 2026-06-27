// Zephyr implementation of the HAL — builds with the nRF Connect SDK (nRF54L) and
// upstream Zephyr (ESP32-S3). LEDs / button / serial / time work from the board's
// standard devicetree; the buzzer + servo (PWM) and analog (ADC) need nodes in
// app.overlay (see README). This is the production firmware for the ESP32-S3
// (built + flashed with `west`); it is the ONLY file that differs per board.
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#if DT_NODE_EXISTS(DT_NODELABEL(pwm0))
#include <zephyr/drivers/pwm.h>
#endif
// Analog input: a `zephyr,user` node with an `io-channels` ADC ref (see overlay).
#define ADC_NODE DT_PATH(zephyr_user)
#if DT_NODE_HAS_PROP(ADC_NODE, io_channels)
#include <zephyr/drivers/adc.h>
#define HAS_ADC 1
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

#ifdef HAS_ADC
static const struct adc_dt_spec g_adc = ADC_DT_SPEC_GET(ADC_NODE);
static bool g_adc_ready = false;
#endif

// ── Non-volatile storage: NVS on the board's `storage` flash partition (192 KB
// on the ESP32-S3, defined in its devicetree). One key holds the autorun program
// text; SAVE writes it, boot reads it (see runtime::init). ──
#define NVS_PARTITION storage_partition
static struct nvs_fs g_nvs;
static bool g_nvs_ready = false;
static const uint16_t kProgramNvsId = 1;

static void store_init() {
  g_nvs.flash_device = FIXED_PARTITION_DEVICE(NVS_PARTITION);
  if (!device_is_ready(g_nvs.flash_device)) return;
  g_nvs.offset = FIXED_PARTITION_OFFSET(NVS_PARTITION);
  struct flash_pages_info info;
  if (flash_get_page_info_by_offs(g_nvs.flash_device, g_nvs.offset, &info) != 0)
    return;
  g_nvs.sector_size = info.size;  // one flash erase-block per NVS sector
  g_nvs.sector_count = 4U;        // a few sectors for wear-levelling headroom
  if (nvs_mount(&g_nvs) == 0) g_nvs_ready = true;
}

namespace hal {

void init() {
  // console + gpio controllers are ready at boot
#ifdef HAS_ADC
  if (adc_is_ready_dt(&g_adc) && adc_channel_setup_dt(&g_adc) == 0)
    g_adc_ready = true;
#endif
  store_init();
}

bool storeSave(const std::string& program) {
  if (!g_nvs_ready) return false;
  ssize_t rc = nvs_write(&g_nvs, kProgramNvsId, program.data(), program.size());
  return rc >= 0;  // bytes written, or 0 if unchanged; <0 on error (e.g. too big)
}

bool storeLoad(std::string& out) {
  if (!g_nvs_ready) return false;
  static char buf[4096];  // one sector — bigger than any real program
  ssize_t rc = nvs_read(&g_nvs, kProgramNvsId, buf, sizeof(buf));
  if (rc <= 0) return false;  // no saved program
  size_t n = (size_t)rc < sizeof(buf) ? (size_t)rc : sizeof(buf);
  out.assign(buf, n);
  return !out.empty();
}

void storeClear() {
  if (g_nvs_ready) nvs_delete(&g_nvs, kProgramNvsId);
}

void pinMode(int pin, int mode) {
  gpio_flags_t flags = (mode == 1)   ? GPIO_OUTPUT_INACTIVE
                       : (mode == 2) ? (GPIO_INPUT | GPIO_PULL_DOWN)   // active-high sensor
                                     : (GPIO_INPUT | GPIO_PULL_UP);    // active-low button
  gpio_pin_configure(gpio_port(pin), gpio_index(pin), flags);
}
void pinWrite(int pin, int level) { gpio_pin_set(gpio_port(pin), gpio_index(pin), level); }
int pinRead(int pin) { return gpio_pin_get(gpio_port(pin), gpio_index(pin)); }

// Analog read via the ADC channel in the overlay (the bytecode pin is ignored —
// the channel/GPIO mapping lives in devicetree). Returns the raw reading.
int analogRead(int /*pin*/) {
#ifdef HAS_ADC
  if (!g_adc_ready) return 0;
  uint16_t buf = 0;
  struct adc_sequence seq = {};
  seq.buffer = &buf;
  seq.buffer_size = sizeof(buf);
  if (adc_sequence_init_dt(&g_adc, &seq) < 0) return 0;
  return adc_read_dt(&g_adc, &seq) == 0 ? (int)buf : 0;
#else
  return 0;  // no ADC channel in the overlay yet
#endif
}

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

// Servo on PWM channel 1 (overlay: LEDC_CH1 on a SEPARATE timer so the 50 Hz
// servo frame is independent of the buzzer's variable tone frequency on ch0).
// A standard hobby servo (SG90): 20 ms frame, 0.5 ms (0°) … 2.5 ms (180°) pulse.
void servo(int /*pin*/, int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  uint32_t pulse = 500000u + (uint32_t)angle * 2000000u / 180u;  // ns
  pwm_set(pwm_dev, 1, 20000000u, pulse, 0);                       // 20 ms = 50 Hz
}
#else
// No pwm node yet → buzzer + servo are no-ops (LEDs/button still work). Add a
// pwm node in app.overlay.
void tone(int, int) {}
void toneOff(int) {}
void servo(int, int) {}
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
