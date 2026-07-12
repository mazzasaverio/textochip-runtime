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
// The buzzer/servo/motor PWM device: our `tc-pwm-buzzer` alias (Nordic DK —
// a `pwm0` LABEL is illegal there, the SoC validation ties pwmN labels to
// NRF_PWMN peripherals), else the `pwm0` nodelabel (ESP32-S3 LEDC overlay).
#if DT_NODE_EXISTS(DT_ALIAS(tc_pwm_buzzer))
#define TC_PWM_NODE DT_ALIAS(tc_pwm_buzzer)
#elif DT_NODE_EXISTS(DT_NODELABEL(pwm0))
#define TC_PWM_NODE DT_NODELABEL(pwm0)
#endif
#ifdef TC_PWM_NODE
#include <zephyr/drivers/pwm.h>
#endif
// Analog input: a `zephyr,user` node with an `io-channels` ADC ref (see overlay).
#define ADC_NODE DT_PATH(zephyr_user)
#if DT_NODE_HAS_PROP(ADC_NODE, io_channels)
#include <zephyr/drivers/adc.h>
#define HAS_ADC 1
#endif
// Digital microphone (I2S) for the edge-AI voice tier — active when the overlay
// marks i2s0 "okay" (the INMP441 wiring). Absent -> hal::aiCapture returns 0.
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2s0), okay)
#include <zephyr/drivers/i2s.h>
#define HAS_MIC 1
#endif

#include "hal.h"

// ── Serial: the chosen console UART is the link to the IDE (Web Serial) ──
static const struct device* const uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

// Interrupt-driven RX (ring buffer) is NORDIC-ONLY by design: the nRF UARTE
// has no usable FIFO in poll mode and LOSES bytes in line bursts (verified at
// bring-up: `LOAD` streamed 6 instructions, the board counted 2-3). The
// ESP32-S3 keeps the bench-verified plain polling (its CDC-ACM console
// selects UART_INTERRUPT_DRIVEN too, so gating on that alone would silently
// change its behavior).
#if defined(CONFIG_UART_INTERRUPT_DRIVEN) && defined(CONFIG_BOARD_NRF54LM20DK)
#define TC_UART_IRQ_RX 1
#endif

#ifdef TC_UART_IRQ_RX
static uint8_t rx_ring[512];
static volatile uint16_t rx_head, rx_tail;

static void uart_rx_isr(const struct device* dev, void* /*user*/) {
  // uart_irq_update() returns void on newer Zephyr — call it, then drain.
  uart_irq_update(dev);
  if (!uart_irq_rx_ready(dev)) return;
  uint8_t b;
  while (uart_fifo_read(dev, &b, 1) == 1) {
    uint16_t next = (uint16_t)((rx_head + 1) % sizeof(rx_ring));
    if (next != rx_tail) {  // on overflow, drop the newest byte
      rx_ring[rx_head] = b;
      rx_head = next;
    }
  }
}

static void serial_rx_init() {
  uart_irq_callback_user_data_set(uart_dev, uart_rx_isr, nullptr);
  uart_irq_rx_enable(uart_dev);
}
#else
static void serial_rx_init() {}
#endif

// ── GPIO: the bytecode carries LOGICAL pin numbers (baked from the browser's
// shared board profile — ESP32-S3 GPIO numbers). Each board's HAL maps them to
// its physical pins (lib/boards.ts + docs/hardware.md state this contract).
// On the ESP32-S3 the mapping is identity. On the Nordic DK, map_pin()
// translates to raw = port*32 + pin.
#if defined(CONFIG_BOARD_NRF54LM20DK)
// Nordic nRF54LM20 DK map (PROVISIONAL — finalize at the bench; keep in sync
// with lib/boards.ts NORDIC_PINS + docs/hardware.md). Phase 1: the three LED
// pins go to the ON-BOARD LEDs (all green — no wiring needed to see the
// semaforo cycle) and button A to the on-board Button 0. Colored external
// LEDs on the header come with phase 2.
static int map_pin(int logical) {
  switch (logical) {
    case 4:  return 1 * 32 + 22;  // red LED    -> LED0 (P1.22, on-board)
    case 2:  return 1 * 32 + 25;  // yellow LED -> LED1 (P1.25, on-board)
    case 1:  return 1 * 32 + 27;  // green LED  -> LED2 (P1.27, on-board)
    case 5:  return 1 * 32 + 13;  // buzzer     -> P1.13 (D5; PWM in phase 2)
    case 6:  return 1 * 32 + 26;  // button A   -> Button 0 (P1.26, on-board)
    case 7:  return 3 * 32 + 0;   // PIR        -> P3.00 (D6)
    case 8:  return 3 * 32 + 1;   // servo      -> P3.01 (D7; PWM in phase 2)
    case 9:  return 0 * 32 + 3;   // analog in  -> P0.03 (D8; ADC in phase 2)
    case 10: return 3 * 32 + 2;   // motor L dir1 -> P3.02 (D10)
    case 11: return 3 * 32 + 3;   // motor L dir2 -> P3.03 (D11)
    case 12: return 1 * 32 + 7;   // motor L pwm  -> P1.07 (D12)
    case 13: return 1 * 32 + 6;   // motor R dir1 -> P1.06 (D13)
    case 14: return 1 * 32 + 5;   // motor R dir2 -> P1.05 (D14)
    case 21: return 2 * 32 + 5;   // motor R pwm  -> P2.05 (D21)
    case 38: return 2 * 32 + 0;   // line left    -> P2.00 (D16)
    case 39: return 2 * 32 + 1;   // line center  -> P2.01 (D17)
    case 40: return 2 * 32 + 2;   // line right   -> P2.02 (D18)
    case 41: return 2 * 32 + 3;   // obstacle     -> P2.03 (D19)
    default: return logical;      // unmapped: raw port*32+pin passthrough
  }
}
#else
static inline int map_pin(int logical) { return logical; }
#endif

static const struct device* gpio_port(int pin) {
#if DT_NODE_EXISTS(DT_NODELABEL(gpio3))
  if (pin >= 96) return DEVICE_DT_GET(DT_NODELABEL(gpio3));
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(gpio2))
  if (pin >= 64) return DEVICE_DT_GET(DT_NODELABEL(gpio2));
#endif
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

#ifdef HAS_MIC
// INMP441 I2S mic on i2s0 (RX; the ESP32-S3 is master). 16 kHz mono = the model's
// sample rate. The DMA fills a pool of 32-bit stereo blocks; aiCapture drains one
// per call and keeps the left channel. PROVISIONAL pins in app.overlay.
static const struct device* const i2s_mic = DEVICE_DT_GET(DT_NODELABEL(i2s0));
#define MIC_FRAMES 256                                           // frames / DMA block
#define MIC_BLOCK_BYTES (MIC_FRAMES * 2 * (int)sizeof(int32_t))  // stereo, 32-bit
K_MEM_SLAB_DEFINE_STATIC(mic_slab, MIC_BLOCK_BYTES, 4, 4);
static bool mic_started = false;

static bool mic_start() {
  if (!device_is_ready(i2s_mic)) return false;
  struct i2s_config cfg = {};
  cfg.word_size = 32;   // INMP441 sends a 24-bit sample left-justified in 32 bits
  cfg.channels = 2;     // stereo frame; the mic drives one channel (L/R -> GND)
  cfg.format = I2S_FMT_DATA_FORMAT_I2S;
  cfg.options = I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_BIT_CLK_MASTER;  // ESP is master
  cfg.frame_clk_freq = 16000;
  cfg.mem_slab = &mic_slab;
  cfg.block_size = MIC_BLOCK_BYTES;
  cfg.timeout = 0;      // non-blocking: i2s_read returns -EAGAIN if no block is ready
  if (i2s_configure(i2s_mic, I2S_DIR_RX, &cfg) != 0) return false;
  return i2s_trigger(i2s_mic, I2S_DIR_RX, I2S_TRIGGER_START) == 0;
}
#endif

namespace hal {

void init() {
  // console + gpio controllers are ready at boot
  serial_rx_init();
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
  int p = map_pin(pin);
  gpio_pin_configure(gpio_port(p), gpio_index(p), flags);
}
void pinWrite(int pin, int level) {
  int p = map_pin(pin);
  gpio_pin_set(gpio_port(p), gpio_index(p), level);
}
int pinRead(int pin) {
  int p = map_pin(pin);
  return gpio_pin_get(gpio_port(p), gpio_index(p));
}

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

#ifdef TC_PWM_NODE
static const struct device* const pwm_dev = DEVICE_DT_GET(TC_PWM_NODE);

// Motor PWM enables (L298N ENA/ENB). On the nRF54 each PWM INSTANCE has ONE
// period, so the two motor enables live on their OWN instance (pwm22, two
// channels) via the `tc-pwm-motor` alias; on the ESP32-S3 LEDC (per-channel
// timers) they share pwm_dev on channels 2/3. Absent alias -> motors are
// silent (LEDs/serial still work), so a mic-less/motor-less board stays green.
#if DT_NODE_EXISTS(DT_ALIAS(tc_pwm_motor))
static const struct device* const pwm_motor = DEVICE_DT_GET(DT_ALIAS(tc_pwm_motor));
#define MOTOR_DEV pwm_motor
#define MOTOR_L_CH 0
#define MOTOR_R_CH 1
#define HAS_MOTORS 1
#elif !defined(CONFIG_BOARD_NRF54LM20DK)
#define MOTOR_DEV pwm_dev
#define MOTOR_L_CH 2
#define MOTOR_R_CH 3
#define HAS_MOTORS 1
#endif

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

#ifdef HAS_MOTORS
// Differential drive on an L298N. Motor pins are LOGICAL (map_pin resolves them
// per board); direction from the sign, speed |v| 0..255 → PWM duty (~1 kHz) on
// the enable channel. Left IN1=10 IN2=11 EN=12, right IN3=13 IN4=14 EN=21.
static void drive_motor(int in1, int in2, int pwmCh, int v) {
  if (v < -255) v = -255;
  if (v > 255) v = 255;
  pinMode(in1, 1);  // OUTPUT
  pinMode(in2, 1);
  // v == 0 coasts BOTH direction pins low, not just duty 0: if the L298N's EN
  // jumper cap was left on (EN tied to 5V), duty alone can't stop the motor —
  // a "stop" must be a stop regardless of how the driver is jumpered.
  pinWrite(in1, v > 0 ? 1 : 0);
  pinWrite(in2, v < 0 ? 1 : 0);
  uint32_t duty = (uint32_t)(v < 0 ? -v : v);
  pwm_set(MOTOR_DEV, pwmCh, 1000000u, 1000000u * duty / 255u, 0);  // ~1 kHz
}
void move(int left, int right) {
  drive_motor(10, 11, MOTOR_L_CH, left);
  drive_motor(13, 14, MOTOR_R_CH, right);
}
#else
void move(int, int) {}  // no motor PWM instance on this board yet
#endif
#else
// No pwm node yet → buzzer + servo + motors are no-ops (LEDs/button still work).
// Add a pwm node in app.overlay.
void tone(int, int) {}
void toneOff(int) {}
void servo(int, int) {}
void move(int, int) {}
#endif

// Edge-AI mic capture (INMP441 over I2S). Drains one DMA block if ready, extracts
// the left channel as int16, and returns the sample count (0 if none ready, or no
// mic node). Non-blocking. The service (src/ai/ai_service.cpp) turns these samples
// into the class VOICE() reads.
int aiCapture(int16_t* out, int n) {
#ifdef HAS_MIC
  if (!mic_started) {
    mic_started = mic_start();
    if (!mic_started) return 0;
  }
  void* block = nullptr;
  size_t size = 0;
  if (i2s_read(i2s_mic, &block, &size) != 0) return 0;  // nothing captured yet
  const int32_t* s = (const int32_t*)block;
  int frames = (int)(size / (2 * sizeof(int32_t)));
  int k = 0;
  for (int i = 0; i < frames && k < n; i++) {
    out[k++] = (int16_t)(s[i * 2] >> 16);  // left channel, top 16 of the 24-bit word
  }
  k_mem_slab_free(&mic_slab, block);
  return k;
#else
  (void)out;
  (void)n;
  return 0;  // no mic node in the overlay -> the edge-AI service reads silence
#endif
}

// Camera capture (edge-AI VISION). STUB for now: returns 0 (no frame), so SEE() reads
// "nothing" on the board. The ESP32-S3-CAM brings this up over the DVP camera interface
// — Zephyr has the driver (drivers/video/video_esp32_dvp.c) + the video subsystem
// (video_dequeue/enqueue); wiring it (an overlay DVP + sensor node) and downscaling to
// the model's 96x96 grayscale is the bench step, like the mic's I2S.
int camCapture(uint8_t* /*out*/, int /*max*/) { return 0; }

uint32_t nowMs() { return (uint32_t)k_uptime_get(); }

int serialReadChar() {
#ifdef TC_UART_IRQ_RX
  if (rx_tail == rx_head) return -1;
  uint8_t b = rx_ring[rx_tail];
  rx_tail = (uint16_t)((rx_tail + 1) % sizeof(rx_ring));
  return (int)b;
#else
  unsigned char c;
  return uart_poll_in(uart_dev, &c) == 0 ? (int)c : -1;
#endif
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
