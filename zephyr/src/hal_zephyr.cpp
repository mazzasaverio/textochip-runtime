// Zephyr implementation of the HAL — builds with the nRF Connect SDK (nRF54L) and
// upstream Zephyr (ESP32-S3). LEDs / button / serial / time work from the board's
// standard devicetree; the buzzer + servo (PWM) and analog (ADC) need nodes in
// the board overlay (zephyr/boards/<board>.overlay). This is the production firmware for the ESP32-S3
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
// The rail probe has its OWN devicetree node. It used to share /zephyr,user,
// which is AREAD()'s: a program calling AREAD() on this board then read the
// supply rail, and no amount of staring at the BASIC could explain the number.
#if DT_NODE_HAS_PROP(ADC_NODE, io_channels)
#define HAS_ADC 1
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(tc_rail)) && \
    DT_NODE_HAS_PROP(DT_NODELABEL(tc_rail), io_channels)
#define HAS_RAIL_ADC 1
#endif
#if defined(HAS_ADC) || defined(HAS_RAIL_ADC)
#include <zephyr/drivers/adc.h>
#endif
// Digital microphone (I2S) for the edge-AI voice tier — active when a mic node
// is wired in the board overlay. Portable across chips: the nRF54L has no I2S
// block, so its INMP441 hangs off the TDM peripheral (a superset that binds to
// Zephyr's I2S API) — the board overlay points the `tc-mic` alias at that node
// (&tdm). The ESP32-S3 keeps its plain `i2s0`. Absent -> aiCapture returns 0.
#if DT_NODE_HAS_STATUS(DT_ALIAS(tc_mic), okay)
#define TC_MIC_NODE DT_ALIAS(tc_mic)
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(i2s0), okay)
#define TC_MIC_NODE DT_NODELABEL(i2s0)
#endif
#if defined(TC_MIC_NODE)
#include <zephyr/drivers/i2s.h>
#define HAS_MIC 1
#endif

#include "hal.h"

#include "arducam_mega.h"

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
  // Clear any line error (framing/overrun/break) — a host opening/closing the
  // VCOM can glitch the line, and an unhandled error can wedge UARTE reception.
  uart_err_check(dev);
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
    case 13: return 3 * 32 + 5;   // motor R dir1 -> P3.05 (D0)  [P1.06/D13 is now ENB/PWM]
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
#ifdef HAS_RAIL_ADC
static const struct adc_dt_spec g_rail = ADC_DT_SPEC_GET(DT_NODELABEL(tc_rail));
static bool g_rail_ready = false;
#endif

// ── Ultrasonic distance (HC-SR04): a TRIG output + ECHO input, wired via two
// board-overlay aliases (hcsr04-trig / hcsr04-echo). HAL-owned pins, so the DIST
// opcode carries none. Absent until the sensor is wired at bring-up, so
// distanceCm() then reports "nothing in range". The ECHO pin is 5V on a real
// HC-SR04 — level-shift or use an HC-SR04P/3.3V module (see docs/hardware.md). ──
#if DT_NODE_EXISTS(DT_ALIAS(hcsr04_trig)) && DT_NODE_EXISTS(DT_ALIAS(hcsr04_echo))
#define HAS_HCSR04 1
static const struct gpio_dt_spec g_hc_trig =
    GPIO_DT_SPEC_GET(DT_ALIAS(hcsr04_trig), gpios);
static const struct gpio_dt_spec g_hc_echo =
    GPIO_DT_SPEC_GET(DT_ALIAS(hcsr04_echo), gpios);
static bool g_hc_ready = false;
#endif

// ── Non-volatile storage: NVS on the board's `storage` flash partition (192 KB
// on the ESP32-S3, defined in its devicetree). One key holds the autorun program
// text; SAVE writes it, boot reads it (see runtime::init). ──
#if defined(CONFIG_BOARD_NRF54LM20DK)
// nRF54L RRAM: NVS reports writes OK but they do NOT read back on this
// no-explicit-erase RRAM — a legacy/garbage storage region confuses NVS's
// flash-page model (bench-proven: nvs_write returns >= 0, nvs_read returns
// -EINVAL immediately after the write, so nothing autoruns on boot). RRAM is
// byte-writable with overwrite, so persist the program as a length-prefixed blob
// written straight to the storage partition via flash_area — no NVS, no erase.
static const struct flash_area* g_fa = nullptr;
static bool g_nvs_ready = false;  // "store ready" (name shared with the NVS path)
static constexpr uint32_t kStoreMagic = 0x54430201;  // 'T''C' 0x02 0x01
static constexpr size_t kStoreWblk = 16;             // RRAM write-block-size (0x10)
static char g_store_buf[4096 + kStoreWblk];          // 16-byte header block + program

static void store_init() {
  if (flash_area_open(FIXED_PARTITION_ID(storage_partition), &g_fa) == 0)
    g_nvs_ready = true;
}
#else
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
#endif

#ifdef HAS_MIC
// INMP441 I2S mic on i2s0 (RX; the ESP32-S3 is master). 16 kHz mono = the model's
// sample rate. The DMA fills a pool of 32-bit stereo blocks; aiCapture drains one
// per call and keeps the left channel. PROVISIONAL pins in the board overlay.
static const struct device* const i2s_mic = DEVICE_DT_GET(TC_MIC_NODE);
#define MIC_FRAMES 256                                           // frames / DMA block
#define MIC_BLOCK_BYTES (MIC_FRAMES * 2 * (int)sizeof(int32_t))  // stereo, 32-bit
// 24 blocks = ~384 ms of buffered audio (2 KB each, 48 KB total). The pool must
// ride out the inference stall: MFCC+invoke block the main loop ~156 ms per
// window, and the old 4-block (64 ms) pool overflowed EVERY window — each
// self-heal restart put a glitch + a ~90 ms hole into every analysed window
// (rail spikes at idle, words never recognized). Bench-sized on the DK.
K_MEM_SLAB_DEFINE_STATIC(mic_slab, MIC_BLOCK_BYTES, 24, 4);
static bool mic_started = false;
// Bench diagnostics: the last mic-start return codes, surfaced by hal::micStatus()
// so the MIC command can say WHY audio is silent. -99 = "step not reached yet".
static int mic_dev_ready = -1;  // 1 = device_is_ready, 0 = not
static int mic_cfg_ret = -99;   // i2s_configure() return
static int mic_trg_ret = -99;   // i2s_trigger(START) return

// DC of one captured block's LEFT channel at int16 scale (blocking, ~16-50 ms),
// or INT32_MIN on timeout. Used by the start-alignment loop below.
static int mic_block_dc(void) {
  for (int tries = 0; tries < 30; tries++) {
    void* block = nullptr;
    size_t size = 0;
    if (i2s_read(i2s_mic, &block, &size) == 0) {
      const int32_t* s = (const int32_t*)block;
      int frames = (int)(size / (2 * sizeof(int32_t)));
      int64_t sum = 0;
      for (int i = 0; i < frames; i++) sum += (s[i * 2] >> 16);
      k_mem_slab_free(&mic_slab, block);
      return frames > 0 ? (int)(sum / frames) : 0;
    }
    k_busy_wait(2000);  // 2 ms — a 256-frame block fills every 16 ms
  }
  return INT32_MIN;
}

// Last measured start DC (int16 scale) — exposed in micStatus for the bench.
static int mic_dc = 0;

// START the RX stream with ALIGNMENT VERIFICATION (bench-proven necessity,
// 2026-07-15): on this TDM the bit alignment of the 24-in-32 sample is NOT
// deterministic across starts — some starts land the data shifted left by k
// bits, scaling BOTH the mic's natural DC (~-1.5k at int16) and the audio by
// 2^k (observed idle "DC" of ~5k and ~11.5k on bad boots). A shifted stream
// crushes/clips speech and the KWS model sees only a huge pedestal, so words
// never classify. Each (re)START re-rolls the alignment: measure the DC of the
// second block (the first can hold the mic's power-up transient) and re-start
// until it lands in the sane band. DROP is allowed from RUNNING and purges the
// queue; START re-arms it.
static bool mic_arm(void) {
  const int kSaneAbsDc = 2000;  // good sessions sit ~1.5k; one bit of shift ~3.1k
                                // (2500 let a ~2.8k roll through — its doubled
                                // noise floor drowned quiet words; bench 2026-07-15)
  for (int attempt = 0; attempt < 12; attempt++) {
    if (attempt > 0) {
      i2s_trigger(i2s_mic, I2S_DIR_RX, I2S_TRIGGER_DROP);
    }
    mic_trg_ret = i2s_trigger(i2s_mic, I2S_DIR_RX, I2S_TRIGGER_START);
    if (mic_trg_ret != 0) return false;
    (void)mic_block_dc();  // skip the first block
    int dc = mic_block_dc();
    if (dc == INT32_MIN) continue;
    mic_dc = dc;
    int a = dc < 0 ? -dc : dc;
    if (a <= kSaneAbsDc) return true;
  }
  return true;  // accept the last roll — micStatus reports dc so the bench sees it
}

static bool mic_start() {
  mic_dev_ready = device_is_ready(i2s_mic) ? 1 : 0;
  if (!mic_dev_ready) return false;
  struct i2s_config cfg = {};
  cfg.word_size = 32;   // INMP441 sends a 24-bit sample left-justified in 32 bits
  cfg.channels = 2;     // stereo frame; the mic drives one channel (L/R -> GND)
  cfg.format = I2S_FMT_DATA_FORMAT_I2S;
  cfg.options = I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_BIT_CLK_MASTER;  // ESP is master
  cfg.frame_clk_freq = 16000;
  cfg.mem_slab = &mic_slab;
  cfg.block_size = MIC_BLOCK_BYTES;
  cfg.timeout = 0;      // non-blocking: i2s_read returns -EAGAIN if no block is ready
  mic_cfg_ret = i2s_configure(i2s_mic, I2S_DIR_RX, &cfg);
  if (mic_cfg_ret != 0) return false;
  return mic_arm();
}

// Read one RX block, self-healing over an overrun. With a fixed 4-block DMA slab,
// any gap in draining (between MIC calls, or a slow tick) fills the pool and the
// driver stops the stream (I2S_STATE_ERROR) — after which every read fails until
// re-armed. PREPARE is REJECTED unless we're actually in ERROR, so issuing it on a
// failed read re-arms an overrun WITHOUT disturbing a merely-empty (still RUNNING)
// queue; START then resumes. Returns true iff a block was obtained. Keeps capture
// continuous for both the bench MIC command and the background voice service.
static bool mic_read_block(void** block, size_t* size) {
  if (i2s_read(i2s_mic, block, size) == 0) return true;
  if (i2s_trigger(i2s_mic, I2S_DIR_RX, I2S_TRIGGER_PREPARE) == 0) {
    // Re-arm through the ALIGNED start (see mic_arm) — a plain START re-rolls
    // the bit alignment and can leave the stream shifted/crushed.
    mic_arm();
  }
  return false;
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
#ifdef HAS_RAIL_ADC
  if (adc_is_ready_dt(&g_rail) && adc_channel_setup_dt(&g_rail) == 0)
    g_rail_ready = true;
#endif
  store_init();
}

#if defined(CONFIG_BOARD_NRF54LM20DK)
// flash_area store (RRAM). Layout: [magic u32][len u32][pad to 16][program bytes].
static inline size_t store_align(size_t n) {
  return (n + (kStoreWblk - 1)) & ~(kStoreWblk - 1);
}
bool storeSave(const std::string& program) {
  if (!g_nvs_ready || program.size() + kStoreWblk > sizeof(g_store_buf)) return false;
  size_t total = store_align(kStoreWblk + program.size());
  memset(g_store_buf, 0, total);
  uint32_t magic = kStoreMagic, len = (uint32_t)program.size();
  memcpy(g_store_buf, &magic, 4);
  memcpy(g_store_buf + 4, &len, 4);
  memcpy(g_store_buf + kStoreWblk, program.data(), program.size());
  return flash_area_write(g_fa, 0, g_store_buf, total) == 0;  // RRAM: overwrite, no erase
}
bool storeLoad(std::string& out) {
  if (!g_nvs_ready) return false;
  uint32_t magic = 0, len = 0;
  if (flash_area_read(g_fa, 0, &magic, 4) != 0 || magic != kStoreMagic) return false;
  if (flash_area_read(g_fa, 4, &len, 4) != 0) return false;
  if (len == 0 || len > sizeof(g_store_buf) - kStoreWblk) return false;  // empty / corrupt
  if (flash_area_read(g_fa, kStoreWblk, g_store_buf, len) != 0) return false;
  out.assign(g_store_buf, len);
  return !out.empty();
}
void storeClear() {
  if (!g_nvs_ready) return;
  memset(g_store_buf, 0, kStoreWblk);  // zero magic = no saved program
  flash_area_write(g_fa, 0, g_store_buf, kStoreWblk);
}
void storeStatus(bool* mounted, int* savedBytes, int* sectorSize, int* sectorCount) {
  if (mounted) *mounted = g_nvs_ready;
  if (sectorSize) *sectorSize = (int)kStoreWblk;
  if (sectorCount) *sectorCount = g_nvs_ready ? (int)g_fa->fa_size : -1;
  if (savedBytes) {
    std::string s;
    *savedBytes = storeLoad(s) ? (int)s.size() : -1;
  }
}
#else
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

void storeStatus(bool* mounted, int* savedBytes, int* sectorSize, int* sectorCount) {
  if (mounted) *mounted = g_nvs_ready;
  if (sectorSize) *sectorSize = g_nvs_ready ? (int)g_nvs.sector_size : -1;
  if (sectorCount) *sectorCount = g_nvs_ready ? (int)g_nvs.sector_count : -1;
  if (savedBytes) {
    if (!g_nvs_ready) {
      *savedBytes = -1;
    } else {
      static char b[4096];
      ssize_t rc = nvs_read(&g_nvs, kProgramNvsId, b, sizeof(b));
      *savedBytes = (int)rc;  // >0 = bytes stored; <=0 = none / error
    }
  }
}
#endif

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

// Ultrasonic distance (HC-SR04): a 10 us TRIG pulse, then time the ECHO HIGH
// width (58 us per cm, round trip). A bounded blocking read (~30 ms timeout =
// out of range). Returns cm, or 400 ("nothing in range") on timeout / no sensor.
int distanceCm() {
#ifdef HAS_HCSR04
  if (!g_hc_ready) {
    if (!gpio_is_ready_dt(&g_hc_trig) || !gpio_is_ready_dt(&g_hc_echo)) return 400;
    gpio_pin_configure_dt(&g_hc_trig, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&g_hc_echo, GPIO_INPUT);
    g_hc_ready = true;
  }
  // 10 us trigger pulse.
  gpio_pin_set_dt(&g_hc_trig, 0);
  k_busy_wait(2);
  gpio_pin_set_dt(&g_hc_trig, 1);
  k_busy_wait(10);
  gpio_pin_set_dt(&g_hc_trig, 0);

  const uint32_t hz = sys_clock_hw_cycles_per_sec();
  const uint32_t timeout_cyc = (uint32_t)((uint64_t)hz * 30000u / 1000000u);
  const uint32_t t0 = k_cycle_get_32();
  while (gpio_pin_get_dt(&g_hc_echo) == 0) {  // wait for the echo to start
    if (k_cycle_get_32() - t0 > timeout_cyc) return 400;
  }
  const uint32_t rise = k_cycle_get_32();
  while (gpio_pin_get_dt(&g_hc_echo) == 1) {  // time the HIGH width
    if (k_cycle_get_32() - rise > timeout_cyc) return 400;
  }
  const uint32_t width_us =
      (uint32_t)((uint64_t)(k_cycle_get_32() - rise) * 1000000u / hz);
  int cm = (int)(width_us / 58u);  // ~58 us per cm (round trip / speed of sound)
  if (cm < 1) cm = 1;
  if (cm > 400) cm = 400;
  return cm;
#else
  return 400;  // no HC-SR04 wired -> nothing in range
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
static bool g_isMoving = false;
void move(int left, int right) {
  drive_motor(10, 11, MOTOR_L_CH, left);
  drive_motor(13, 14, MOTOR_R_CH, right);
  g_isMoving = (left != 0 || right != 0);
}
bool isMoving() { return g_isMoving; }
#else
void move(int, int) {}  // no motor PWM instance on this board yet
bool isMoving() { return false; }
#endif
#else
// No pwm node yet → buzzer + servo + motors are no-ops (LEDs/button still work).
// Add a pwm node in the board overlay (zephyr/boards/<board>.overlay).
void tone(int, int) {}
void toneOff(int) {}
void servo(int, int) {}
void move(int, int) {}
bool isMoving() { return false; }
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
  if (!mic_read_block(&block, &size)) return 0;  // none ready (self-heals an overrun)
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

// Bench diagnostic (see hal.h): report the mic-start state so `MIC` can explain a
// silent capture. `started=1 cfg=0 trg=0` with n=0 -> the TDM started but no DMA
// block filled (clock/SCK or wiring); `started=0` with a negative cfg/trg -> the
// driver rejected the config or trigger (that errno points at the cause).
std::string micStatus() {
#ifdef HAS_MIC
  return "started=" + std::to_string(mic_started ? 1 : 0) +
         " ready=" + std::to_string(mic_dev_ready) +
         " cfg=" + std::to_string(mic_cfg_ret) + " trg=" + std::to_string(mic_trg_ret) +
         " dc=" + std::to_string(mic_dc);
#else
  return "no-mic-node";
#endif
}

// Bench diagnostic (see hal.h): sample the mic pads and count toggles. The pads
// are decoded from the SAME pinctrl group the TDM/I2S uses (psel & 0x1FF =
// port*32+pin), so the probe can never drift from the real mux. Nordic-only for
// now (the ESP32 pinctrl model differs); elsewhere returns "n/a".
std::string micPinsProbe() {
#if defined(HAS_MIC) && DT_NODE_EXISTS(DT_CHILD(DT_NODELABEL(tc_tdm_default), group1))
#define TC_TDM_PSEL(i) \
  (DT_PROP_BY_IDX(DT_CHILD(DT_NODELABEL(tc_tdm_default), group1), psels, i) & 0x1FF)
  static const int pads[3] = {TC_TDM_PSEL(0), TC_TDM_PSEL(1), TC_TDM_PSEL(2)};
  static const char* names[3] = {"sck", "ws", "sd"};
  // Connect the input buffers so gpio_pin_get_raw reads the pad level even while
  // the peripheral owns the pin (readback only — the PSEL keeps driving).
  for (int k = 0; k < 3; k++) {
    gpio_pin_configure(gpio_port(pads[k]), gpio_index(pads[k]), GPIO_INPUT);
  }
  int16_t drain[64];
  aiCapture(drain, 64);  // make sure capture is started (and self-heal if stalled)
  long tog[3] = {0, 0, 0};
  long ones[3] = {0, 0, 0};
  int last[3] = {-1, -1, -1};
  const int N = 60000;  // a few ms of ~MHz-rate sampling — plenty vs a 1 MHz SCK
  for (int i = 0; i < N; i++) {
    if ((i & 2047) == 0) aiCapture(drain, 64);  // keep the DMA drained mid-probe
    for (int k = 0; k < 3; k++) {
      int v = gpio_pin_get_raw(gpio_port(pads[k]), gpio_index(pads[k]));
      if (last[k] >= 0 && v != last[k]) tog[k]++;
      if (v > 0) ones[k]++;
      last[k] = v;
    }
  }
  std::string s = "pads";
  for (int k = 0; k < 3; k++) {
    s += " " + std::string(names[k]) + "=P" + std::to_string(pads[k] / 32) + "." +
         std::to_string(pads[k] % 32) + " tog=" + std::to_string(tog[k]) +
         " hi%=" + std::to_string(ones[k] * 100 / N);
  }
  return s;
#undef TC_TDM_PSEL
#else
  return "n/a";
#endif
}

// Bench diagnostic (see hal.h): drain one raw block as interleaved 32-bit L/R words.
int aiCaptureRaw(int32_t* out, int n) {
#ifdef HAS_MIC
  if (!mic_started) {
    mic_started = mic_start();
    if (!mic_started) return 0;
  }
  void* block = nullptr;
  size_t size = 0;
  if (!mic_read_block(&block, &size)) return 0;
  const int32_t* s = (const int32_t*)block;
  int words = (int)(size / sizeof(int32_t));  // interleaved L,R,L,R…
  int k = 0;
  for (int i = 0; i < words && k < n; i++) out[k++] = s[i];
  k_mem_slab_free(&mic_slab, block);
  return k;
#else
  (void)out;
  (void)n;
  return 0;
#endif
}

// Camera capture (edge-AI VISION). STUB for now: returns 0 (no frame), so SEE() reads
// "nothing" on the board. The ESP32-S3-CAM brings this up over the DVP camera interface
// — Zephyr has the driver (drivers/video/video_esp32_dvp.c) + the video subsystem
// (video_dequeue/enqueue); wiring it (an overlay DVP + sensor node) and downscaling to
// the model's 96x96 grayscale is the bench step, like the mic's I2S.
int camCapture(uint8_t* /*out*/, int /*max*/) { return 0; }

// RGB capture for the colour vision path — the Arducam Mega over SPI
// (src/arducam_mega.cpp). Cooperative by construction: a poll either advances
// the exposure by one register read or hands over the next slice of the frame,
// so the VM keeps ticking. With no camera node in the board overlay, or nothing
// answering on the bus, it reports 0 bytes and SEE() reads "nothing".
int camCaptureRGB(uint8_t* out, int max) { return arducam_capture_rgb(out, max); }

std::string camProbe() { return arducam_probe(); }

// Is anything ALIVE on the other end of the MISO wire?
//
// The id probe says 0x00 for two different worlds — "the module has no power"
// and "the MISO wire is not in its hole" — and a maker cannot tell them apart by
// looking. This can: read the pad with the internal pull UP and then with it
// DOWN. A line nobody drives follows the pull (1 then 0) and is therefore OPEN;
// a powered module holds its output stage, so the level does not follow. Same
// idea as micPinsProbe, one wire instead of three.
#if DT_NODE_EXISTS(DT_NODELABEL(arducam))
#define TC_SPI_PSEL(i) \
  (DT_PROP_BY_IDX(DT_CHILD(DT_NODELABEL(tc_spi22_default), group1), psels, i) & 0x1FF)
#endif

// BIT-BANG the same read, with the SPI peripheral out of the way.
//
// Every other probe here shares one assumption: that the SPIM instance drives
// the bus the way we asked. This one drops that assumption and clocks the four
// pads by hand — mode 0, MSB first, exactly the transaction readReg() performs.
//
//   0x81..0x84 -> the module and all six wires are FINE, and the fault is in the
//                 peripheral's configuration (which we can then go and fix).
//   0x00/0xFF  -> the bus controller was never the problem: it is the wiring or
//                 the module itself, and no amount of driver work will help.
//
// It leaves the pads as GPIO, so the board wants a reset afterwards before the
// normal camera path is used again. Bench command, not a runtime path.
// TEMPORARY (2026-08-01): report the board's supply rail in millivolts. See the
// note in the DK overlay — this exists to settle whether the camera is simply
// under-powered, and goes away with the answer.
std::string railProbe() {
#ifdef HAS_RAIL_ADC
  if (!g_rail_ready) return "adc not ready";
  int16_t buf = 0;
  struct adc_sequence seq = {};
  seq.buffer = &buf;
  seq.buffer_size = sizeof(buf);
  if (adc_sequence_init_dt(&g_rail, &seq) < 0) return "adc init failed";
  if (adc_read_dt(&g_rail, &seq) != 0) return "adc read failed";
  int32_t mv = buf;
  if (adc_raw_to_millivolts_dt(&g_rail, &mv) < 0) return "adc scale failed";

  // And again UNDER LOAD. A module that browns out its own supply at power-up
  // is selected, clocked and mute — exactly what the camera looks like — and a
  // rail measured only at rest would never show it.
  int32_t lo = mv, hi = mv;
  for (int n = 0; n < 200; n++) {
    arducam_probe_tick();
    int16_t b = 0;
    struct adc_sequence sq = {};
    sq.buffer = &b;
    sq.buffer_size = sizeof(b);
    if (adc_sequence_init_dt(&g_rail, &sq) < 0) break;
    if (adc_read_dt(&g_rail, &sq) != 0) break;
    int32_t v = b;
    if (adc_raw_to_millivolts_dt(&g_rail, &v) < 0) break;
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  return "VDD = " + std::to_string(mv) + " mV (under load " + std::to_string(lo) +
         ".." + std::to_string(hi) + " mV)" +
         (mv - lo > 150 ? "  <-- THE RAIL SAGS while the camera draws: it is a "
                          "power problem, not a bus problem"
                        : "") +
         (mv < 3200 ? "  <-- the Arducam wants 3.3V and Nordic's own sample for "
                      "this camera on this DK says to set it there (Board "
                      "Configurator). The mic does not care, which is why it "
                      "works and the camera does not"
                    : "  (3.3V: what the camera asks for)");
#else
  return "no rail channel on this board";
#endif
}

// Which P1 pads are electrically healthy? P1.02 turned out to be shorted to
// ground on the board itself, so the camera's clock needs another home — and an
// nRF SPIM instance cannot spread its pins across two GPIO ports (the same rule
// the mic and the motor PWM taught us), so the replacement must also be on P1.
// Pads already spoken for (motors 5/6/7, buzzer 13, mic 14/23/31) are skipped:
// driving a motor enable to probe it would twitch a wheel.
std::string padScan() {
  std::string out;
  const int used[] = {5, 6, 7, 13, 14, 22, 23, 25, 26, 31};
  for (int i = 0; i <= 15; i++) {
    bool skip = false;
    for (int u : used)
      if (u == i) skip = true;
    if (skip) continue;
    const int pin = 32 + i;  // port 1
    const struct device* dv = gpio_port(pin);
    const gpio_pin_t ix = gpio_index(pin);
    gpio_pin_configure(dv, ix, GPIO_OUTPUT_HIGH | GPIO_INPUT);
    k_busy_wait(200);
    int hi = gpio_pin_get_raw(dv, ix);
    gpio_pin_configure(dv, ix, GPIO_OUTPUT_LOW | GPIO_INPUT);
    k_busy_wait(200);
    int lo = gpio_pin_get_raw(dv, ix);
    gpio_pin_configure(dv, ix, GPIO_INPUT);
    out += " P1." + std::to_string(i) + "=" +
           (hi == 0 ? "SHORT-GND" : lo == 1 ? "short-vdd" : "ok");
  }
  return out;
}

std::string camBitbangProbe() {
#if DT_NODE_EXISTS(DT_NODELABEL(arducam))
  const int sck = TC_SPI_PSEL(0);
  const int miso = TC_SPI_PSEL(1);
  const int mosi = TC_SPI_PSEL(2);
  // cs-gpios sits on the BUS node, not the device (Zephyr's SPI convention).
  const struct device* csDev =
      DEVICE_DT_GET(DT_SPI_DEV_CS_GPIOS_CTLR(DT_NODELABEL(arducam)));
  const gpio_pin_t csPin = DT_SPI_DEV_CS_GPIOS_PIN(DT_NODELABEL(arducam));
  if (!device_is_ready(csDev)) return "cs gpio port not ready";

  // Try BOTH pad assignments: the wires being swapped is the commonest bench
  // mistake, and asking the board is cheaper than asking the maker to re-count
  // six identical jumpers.
  auto attempt = [&](int outPin, int inPin, uint8_t* out3) {
    gpio_pin_configure(gpio_port(sck), gpio_index(sck), GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(gpio_port(outPin), gpio_index(outPin), GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(gpio_port(inPin), gpio_index(inPin), GPIO_INPUT);
    gpio_pin_configure(csDev, csPin, GPIO_OUTPUT);
    gpio_pin_set_raw(csDev, csPin, 1);  // idle high (active low)
    k_busy_wait(50);

    auto xfer = [&](uint8_t o) {
      uint8_t in = 0;
      for (int b = 7; b >= 0; b--) {
        gpio_pin_set_raw(gpio_port(outPin), gpio_index(outPin), (o >> b) & 1);
        k_busy_wait(2);
        gpio_pin_set_raw(gpio_port(sck), gpio_index(sck), 1);  // sample on rising
        k_busy_wait(2);
        in = (uint8_t)((in << 1) |
                       (gpio_pin_get_raw(gpio_port(inPin), gpio_index(inPin)) & 1));
        gpio_pin_set_raw(gpio_port(sck), gpio_index(sck), 0);
        k_busy_wait(2);
      }
      return in;
    };

    gpio_pin_set_raw(csDev, csPin, 0);  // select
    k_busy_wait(50);
    xfer(0x40);  // REG_SENSOR_ID, read (high bit clear)
    for (int i = 0; i < 3; i++) out3[i] = xfer(0x00);
    gpio_pin_set_raw(csDev, csPin, 1);
    k_busy_wait(50);
  };

  uint8_t asWired[3] = {0, 0, 0}, swapped[3] = {0, 0, 0}, afterReset[3] = {0, 0, 0};
  attempt(mosi, miso, asWired);
  attempt(miso, mosi, swapped);  // MOSI/MISO exchanged

  // Third try: RESET first. Arducam's own bring-up writes REG_SENSOR_RESET and
  // waits before anything else, and a module sitting in a state that needs it
  // would look exactly like this one — selected, clocked, mute.
  {
    gpio_pin_configure(gpio_port(sck), gpio_index(sck), GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(gpio_port(mosi), gpio_index(mosi), GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(gpio_port(miso), gpio_index(miso), GPIO_INPUT);
    gpio_pin_configure(csDev, csPin, GPIO_OUTPUT);
    auto put = [&](uint8_t o) {
      for (int b = 7; b >= 0; b--) {
        gpio_pin_set_raw(gpio_port(mosi), gpio_index(mosi), (o >> b) & 1);
        k_busy_wait(2);
        gpio_pin_set_raw(gpio_port(sck), gpio_index(sck), 1);
        k_busy_wait(4);
        gpio_pin_set_raw(gpio_port(sck), gpio_index(sck), 0);
        k_busy_wait(2);
      }
    };
    gpio_pin_set_raw(csDev, csPin, 1);
    k_busy_wait(100);
    gpio_pin_set_raw(csDev, csPin, 0);
    k_busy_wait(50);
    put(0x87);  // REG_SENSOR_RESET with the WRITE bit
    put(0x40);  // RESET_ENABLE
    gpio_pin_set_raw(csDev, csPin, 1);
    k_msleep(800);  // the sensor reboots
    attempt(mosi, miso, afterReset);
  }

  char buf[128];
  snprintf(buf, sizeof(buf),
           "as-wired=%02x %02x %02x  swapped=%02x %02x %02x  after-reset=%02x %02x %02x",
           asWired[0], asWired[1], asWired[2], swapped[0], swapped[1], swapped[2],
           afterReset[0], afterReset[1], afterReset[2]);
  auto isId = [](uint8_t v) { return v >= 0x81 && v <= 0x84; };
  bool okWired = isId(asWired[0]) || isId(asWired[1]) || isId(asWired[2]);
  bool okSwap = isId(swapped[0]) || isId(swapped[1]) || isId(swapped[2]);
  bool okReset = isId(afterReset[0]) || isId(afterReset[1]) || isId(afterReset[2]);
  return std::string("cs=") + csDev->name + "." + std::to_string(csPin) + " " +
         buf +
         (okWired   ? "  <-- ANSWERS AS WIRED: the fault is the SPI peripheral setup"
          : okSwap  ? "  <-- ANSWERS SWAPPED: the MOSI and MISO wires are exchanged"
          : okReset ? "  <-- ANSWERS AFTER RESET: the module needs REG_SENSOR_RESET "
                      "at start-up"
                    : "  <-- silent every way: not the controller, not a swap, not "
                      "a missing reset");
#else
  return "no camera node in the board overlay";
#endif
}

std::string camPinsProbe() {
#if DT_NODE_EXISTS(DT_NODELABEL(arducam))
  const int sck = TC_SPI_PSEL(0);
  const int miso = TC_SPI_PSEL(1);
  const int mosi = TC_SPI_PSEL(2);

  // 1. What is at the far end of each wire? A pad whose level FOLLOWS our own
  //    pull is looking at either a high-impedance input or nothing at all — the
  //    two are indistinguishable from here, which is why the bench procedure is
  //    to move one wire's module end onto the module's GND pad: a wire that
  //    conducts then reads 0 under a pull-up, and a broken one keeps following
  //    the pull. A dupont jumper that fails inside its insulation looks perfect.
  struct Pad {
    const char* name;
    int pin;
  };
  const Pad pads[] = {{"sck", sck}, {"miso", miso}, {"mosi", mosi}};
  std::string report;
  // DETACH the pads from the SPI peripheral first. SCK and MOSI are peripheral
  // OUTPUTS: a driver holding the pad low beats any pull we apply, so measuring
  // them while the peripheral owns them reports "grounded" for a perfectly good
  // wire — which is exactly the wrong answer this probe gave, and it sent a
  // maker chasing a wire that was never the problem.
  const uint32_t savedSck = NRF_SPIM22->PSEL.SCK;
  const uint32_t savedMosi = NRF_SPIM22->PSEL.MOSI;
  const uint32_t savedMiso = NRF_SPIM22->PSEL.MISO;
  NRF_SPIM22->PSEL.SCK = 0x80000000u;   // disconnected
  NRF_SPIM22->PSEL.MOSI = 0x80000000u;
  NRF_SPIM22->PSEL.MISO = 0x80000000u;
  for (const Pad& p : pads) {
    const struct device* dv = gpio_port(p.pin);
    const gpio_pin_t ix = gpio_index(p.pin);
    gpio_pin_configure(dv, ix, GPIO_INPUT | GPIO_PULL_UP);
    k_msleep(2);
    int u = gpio_pin_get_raw(dv, ix);
    gpio_pin_configure(dv, ix, GPIO_INPUT | GPIO_PULL_DOWN);
    k_msleep(2);
    int d = gpio_pin_get_raw(dv, ix);
    gpio_pin_configure(dv, ix, GPIO_INPUT);
    // Then DRIVE it, which settles what a pull cannot: a pad we hold high that
    // still reads low is fighting a hard short to ground. A pull-up loses that
    // argument to anything, including our own peripheral, so the pull test
    // alone can (and did) accuse an innocent wire.
    gpio_pin_configure(dv, ix, GPIO_OUTPUT_HIGH | GPIO_INPUT);
    k_busy_wait(200);
    int drivenHigh = gpio_pin_get_raw(dv, ix);
    gpio_pin_configure(dv, ix, GPIO_OUTPUT_LOW | GPIO_INPUT);
    k_busy_wait(200);
    int drivenLow = gpio_pin_get_raw(dv, ix);
    gpio_pin_configure(dv, ix, GPIO_INPUT);
    const char* verdict =
        (drivenHigh == 0)      ? "SHORTED TO GROUND (held low even when driven high)"
        : (drivenLow == 1)     ? "shorted to a supply (held high even when driven low)"
        : (u == 1 && d == 0)   ? "free (follows the pull: open, or a plain input at the far end)"
        : (u == 0 && d == 0)   ? "pulled low by something (but not a short)"
                               : "pulled high by something (but not a short)";
    report += std::string(report.empty() ? "" : " · ") + p.name + "=P" +
              std::to_string(p.pin / 32) + "." + std::to_string(p.pin % 32) + " " +
              verdict;
  }
  NRF_SPIM22->PSEL.SCK = savedSck;
  NRF_SPIM22->PSEL.MOSI = savedMosi;
  NRF_SPIM22->PSEL.MISO = savedMiso;

  // 2. Does this SPI instance actually own the pads we wired to? The mic taught
  //    the lesson (a TDM instance that silently no-ops on the wrong port), and
  //    until it is checked, "the module does not answer" and "we never asked"
  //    look the same. Ask the PERIPHERAL, not the pads. Reading SCK/MOSI as GPIO inputs (the
  // first attempt) DETACHES the peripheral output on nRF, so the probe broke
  // exactly what it was measuring and reported a flat clock for a bus that may
  // be fine. PSEL says which pads this instance owns; ENABLE says whether it is
  // switched on at all. Neither touches a thing.
  uint32_t enable = NRF_SPIM22->ENABLE;
  uint32_t pselSck = NRF_SPIM22->PSEL.SCK;
  uint32_t pselMosi = NRF_SPIM22->PSEL.MOSI;
  uint32_t pselMiso = NRF_SPIM22->PSEL.MISO;
  auto psel = [](uint32_t v) {
    if (v & 0x80000000u) return std::string("off");
    return "P" + std::to_string((v >> 5) & 0x7) + "." + std::to_string(v & 0x1F);
  };

  return report + " · spim22 enable=" + std::to_string(enable) +
         " sck=" + psel(pselSck) +
         " mosi=" + psel(pselMosi) + " miso=" + psel(pselMiso) +
         (enable == 0 ? "  <-- the instance is OFF: it never drove the bus"
                      : "");
#else
  return "no camera node in the board overlay";
#endif
}

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
