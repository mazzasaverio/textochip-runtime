/*
 * Arducam Mega (SPI) camera — the COLOUR eye behind SEE() / SEEX() / SEESIZE().
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The SPI register protocol below (register numbers, the sensor-state poll, the
 * burst-FIFO read handshake, the big-endian RGB565 layout) follows Arducam's own
 * Mega driver and Nordic's Zephyr port of it in the nRF Edge AI add-on
 * (applications/person_detection/src/drivers/arducam_mega.c, Apache-2.0,
 * Copyright (c) 2023 Arducam Technology Co., Ltd. / (c) 2026 Nordic Semiconductor
 * ASA) — the same camera on the same DK, which is why it is the reference.
 *
 * This is NOT that driver, nor Zephyr's own (drivers/video/video_arducam_mega.c
 * — the devicetree binding here IS the upstream one). Both are full video-API
 * drivers: work queue, timers, buffer FIFOs, CONFIG_VIDEO, built for streaming. The runtime
 * needs one thing: fill the vision service's frame a chunk at a time, from a
 * cooperative poll that must never block. So this is a small state machine over
 * the same wire protocol, which happens to fit better: the Mega's burst read is
 * ALREADY resumable across SPI transactions, so "give me the next 1 KB" maps
 * onto it one-to-one, and waiting for the exposure costs one register read per
 * poll instead of a blocking sleep loop.
 *
 * Wiring, pins and bring-up: docs/hardware.md + docs/edge-ai.md. Absent camera
 * (no devicetree node, or nothing answering on the bus) is not an error: the
 * capture reports 0 bytes, so SEE() reads "nothing" and every program still runs.
 */
#include <stdint.h>
#include <string.h>

#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#include "arducam_mega.h"

#if DT_NODE_EXISTS(DT_NODELABEL(arducam)) && DT_NODE_HAS_STATUS(DT_NODELABEL(arducam), okay)

namespace {

// ── the Mega's SPI register map (see the file header for provenance) ──────────
constexpr uint8_t REG_SENSOR_RESET = 0x07;      // write RESET_ENABLE to reboot the sensor
constexpr uint8_t REG_FORMAT = 0x20;            // pixel format (JPEG / RGB565 / YUV)
constexpr uint8_t REG_CAPTURE_RESOLUTION = 0x21;
constexpr uint8_t REG_SENSOR_ID = 0x40;
constexpr uint8_t REG_YEAR = 0x41, REG_MONTH = 0x42, REG_DAY = 0x43;
constexpr uint8_t REG_FPGA_VERSION = 0x49;
constexpr uint8_t REG_SENSOR_STATE = 0x44;      // also ARDUCHIP_TRIG: bit 2 = capture done
constexpr uint8_t REG_FIFO = 0x04;              // FIFO control: clear / start
constexpr uint8_t REG_FIFO_SIZE1 = 0x45, REG_FIFO_SIZE2 = 0x46, REG_FIFO_SIZE3 = 0x47;

constexpr uint8_t FIFO_CLEAR = 0x01;
constexpr uint8_t FIFO_START = 0x02;
constexpr uint8_t CAP_DONE = 0x04;              // in REG_SENSOR_STATE (ARDUCHIP_TRIG)
constexpr uint8_t STATE_IDLE = 0x02;            // (REG_SENSOR_STATE & 0x03) when idle
constexpr uint8_t RESET_ENABLE = 0x40;
constexpr uint8_t BURST_FIFO_READ = 0x3C;

constexpr uint8_t PIXFMT_RGB565 = 0x02;
constexpr uint8_t RES_96X96 = 0x0A;

// The vision service's frame (src/ai/vision_service.cpp). The camera is asked
// for exactly this, so no scaling is needed on our side.
constexpr int kWidth = 96;
constexpr int kHeight = 96;
constexpr int kPixels = kWidth * kHeight;

// A capture that never reports "done" must not wedge the vision service: after
// this long we give up on the frame and start another (a camera unplugged
// mid-run then simply reads "nothing" instead of freezing SEE() forever).
constexpr uint32_t kCaptureTimeoutMs = 1000;

const struct spi_dt_spec g_bus = SPI_DT_SPEC_GET(
    DT_NODELABEL(arducam),
    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);

// Bytes pulled off the bus per poll. 512 RGB565 bytes = 256 pixels = 768 bytes
// of RGB888 out, which is just under the 1 KB the vision service asks for, so a
// poll is one short SPI transaction (~0.5 ms at 8 MHz) and the VM keeps ticking.
constexpr int kScratch = 512;
uint8_t g_scratch[kScratch];

enum State { kUninit, kFailed, kIdle, kCapturing, kReading };
State g_state = kUninit;
uint32_t g_fifoLeft = 0;     // bytes the camera still holds for this frame
int g_pixelsDone = 0;        // pixels handed to the service for this frame
bool g_firstRead = true;     // the burst read's first transaction sends a dummy byte
uint32_t g_captureStart = 0;
uint8_t g_sensorId = 0;
// What the last probe actually read from REG_SENSOR_ID (-2 = the SPI device was
// never ready, negative = transfer error). A failure that cannot say what it saw
// sends the maker to check all six wires instead of the one that is wrong.
int g_lastId = -2;
uint8_t g_fw[4] = {0, 0, 0, 0};  // year, month, day, fpga version (bench info)

int writeReg(uint8_t reg, uint8_t value) {
  uint8_t addr = (uint8_t)(reg | 0x80);  // the high bit marks a WRITE
  struct spi_buf tx[2] = {{&addr, 1}, {&value, 1}};
  struct spi_buf_set txs = {tx, 2};
  return spi_write_dt(&g_bus, &txs);
}

// A read clocks out the address then discards two bytes before the value: the
// Mega answers one byte late, and the burst logic eats another.
int readReg(uint8_t reg) {
  uint8_t addr = (uint8_t)(reg & 0x7F);
  uint8_t discard[2] = {0, 0};
  uint8_t value = 0;
  struct spi_buf tx[1] = {{&addr, 1}};
  struct spi_buf_set txs = {tx, 1};
  struct spi_buf rx[3] = {{&discard[0], 1}, {&discard[1], 1}, {&value, 1}};
  struct spi_buf_set rxs = {rx, 3};
  if (spi_transceive_dt(&g_bus, &txs, &rxs) != 0) return -1;
  return (int)value;
}

// Continue the burst read. The first transaction of a frame sends the command
// plus one dummy byte; later ones re-send only the command, and the camera picks
// up where it left off — which is exactly the resumable read the service wants.
int readFifo(uint8_t* dst, int len, bool first) {
  uint8_t cmd[2] = {BURST_FIFO_READ, 0x00};
  int cmdLen = first ? 2 : 1;
  struct spi_buf tx[1] = {{cmd, (size_t)cmdLen}};
  struct spi_buf_set txs = {tx, 1};
  struct spi_buf rx[2] = {{cmd, (size_t)cmdLen}, {dst, (size_t)len}};
  struct spi_buf_set rxs = {rx, 2};
  return spi_transceive_dt(&g_bus, &txs, &rxs);
}

// Wait for the sensor to finish a configuration write. Only used during setup
// (once), so a bounded sleep here costs nothing at run time.
bool awaitIdle(int tries) {
  while (tries-- > 0) {
    int st = readReg(REG_SENSOR_STATE);
    if (st >= 0 && (st & 0x03) == STATE_IDLE) return true;
    k_msleep(2);
  }
  return false;
}

// One-time setup: reboot the sensor, check something answers, then lock it to
// the frame the vision service wants (96x96 RGB565).
bool setup() {
  g_lastId = -2;  // "the bus was never usable"
  if (!spi_is_ready_dt(&g_bus)) return false;

  writeReg(REG_SENSOR_RESET, RESET_ENABLE);
  k_msleep(1000);  // the sensor reboots; the datasheet's own driver waits this long

  int id = readReg(REG_SENSOR_ID);
  g_lastId = id;  // remembered for the bench probe, which must say WHAT it saw
  // 0x81/0x83 = the 5MP variants, 0x82/0x84 = 3MP. Anything else (0x00 / 0xFF)
  // means nothing is answering: no camera, or MISO/CS not wired.
  if (id < 0x81 || id > 0x84) return false;
  g_sensorId = (uint8_t)id;

  g_fw[0] = (uint8_t)(readReg(REG_YEAR) & 0x3F);
  g_fw[1] = (uint8_t)(readReg(REG_MONTH) & 0x0F);
  g_fw[2] = (uint8_t)(readReg(REG_DAY) & 0x1F);
  g_fw[3] = (uint8_t)(readReg(REG_FPGA_VERSION) & 0xFF);

  awaitIdle(3);
  if (writeReg(REG_FORMAT, PIXFMT_RGB565) != 0) return false;
  awaitIdle(30);
  if (writeReg(REG_CAPTURE_RESOLUTION, RES_96X96) != 0) return false;
  awaitIdle(10);
  return true;
}

void startCapture() {
  writeReg(REG_FIFO, FIFO_CLEAR);
  writeReg(REG_FIFO, FIFO_START);
  g_fifoLeft = 0;
  g_pixelsDone = 0;
  g_firstRead = true;
  g_captureStart = (uint32_t)k_uptime_get();
  g_state = kCapturing;
}

}  // namespace

int arducam_capture_rgb(uint8_t* out, int max) {
  if (out == nullptr || max < 3) return 0;

  switch (g_state) {
    case kFailed:
      return 0;
    case kUninit:
      if (!setup()) {
        g_state = kFailed;  // no camera on the bus: SEE() reads "nothing", forever quiet
        return 0;
      }
      g_state = kIdle;
      [[fallthrough]];
    case kIdle:
      startCapture();
      return 0;

    case kCapturing: {
      int st = readReg(REG_SENSOR_STATE);
      if (st < 0 || !(st & CAP_DONE)) {
        // Still exposing. One register read per poll — the VM keeps running.
        if ((uint32_t)k_uptime_get() - g_captureStart > kCaptureTimeoutMs) g_state = kIdle;
        return 0;
      }
      // The camera reports how many bytes it holds. For 96x96 RGB565 that should
      // be 96*96*2 = 18432; the CAM bench command prints it, because a firmware
      // that prepends a marker would show up here as a couple of bytes more.
      int s1 = readReg(REG_FIFO_SIZE1), s2 = readReg(REG_FIFO_SIZE2), s3 = readReg(REG_FIFO_SIZE3);
      if (s1 < 0 || s2 < 0 || s3 < 0) {
        g_state = kIdle;
        return 0;
      }
      g_fifoLeft = (uint32_t)s1 | ((uint32_t)s2 << 8) | ((uint32_t)s3 << 16);
      if (g_fifoLeft < 2) {
        g_state = kIdle;
        return 0;
      }
      g_state = kReading;
      return 0;
    }

    case kReading: {
      int wantPixels = max / 3;
      if (wantPixels > kScratch / 2) wantPixels = kScratch / 2;
      if (wantPixels > (int)(g_fifoLeft / 2)) wantPixels = (int)(g_fifoLeft / 2);
      if (wantPixels > kPixels - g_pixelsDone) wantPixels = kPixels - g_pixelsDone;
      if (wantPixels <= 0) {  // frame delivered (or the camera ran dry) -> next frame
        g_state = kIdle;
        return 0;
      }
      if (readFifo(g_scratch, wantPixels * 2, g_firstRead) != 0) {
        g_state = kIdle;
        return 0;
      }
      g_firstRead = false;
      g_fifoLeft -= (uint32_t)(wantPixels * 2);
      g_pixelsDone += wantPixels;

      // RGB565, big-endian per pixel (R in bits 15..11), expanded to RGB888 by
      // replicating the high bits — the same expansion the IDE's colour probe
      // uses, so what the board sees matches what the probe predicted.
      for (int i = 0; i < wantPixels; i++) {
        uint16_t p = (uint16_t)((uint16_t)g_scratch[i * 2] << 8 | g_scratch[i * 2 + 1]);
        uint8_t r5 = (uint8_t)((p >> 11) & 0x1F);
        uint8_t g6 = (uint8_t)((p >> 5) & 0x3F);
        uint8_t b5 = (uint8_t)(p & 0x1F);
        out[i * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
        out[i * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
        out[i * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
      }
      if (g_pixelsDone >= kPixels) g_state = kIdle;  // whole frame handed over
      return wantPixels * 3;
    }
  }
  return 0;
}

std::string arducam_probe() {
  // Force the one-time setup so the answer is about the camera, not about
  // whether a program happens to be running.
  if (g_state == kUninit) g_state = setup() ? kIdle : kFailed;
  if (g_state == kFailed) {
    // WHAT was read decides which wire to look at, and the difference costs a
    // maker an hour of checking all six:
    //   -2   the SPI device itself is not ready — firmware/devicetree, not wiring
    //   0x00 MISO sits low: the module has no power, or MISO is not connected
    //        (the pinctrl group pulls down, so an open line reads zero)
    //   0xFF MISO floats high: powered but never selected — CS on the wrong pin
    //   else something answered but is not a Mega (0x81..0x84)
    char why[120];
    if (g_lastId == -2)
      snprintf(why, sizeof(why), "absent (SPI device not ready — firmware, not wiring)");
    else if (g_lastId < 0)
      snprintf(why, sizeof(why), "absent (SPI transfer failed, err=%d)", g_lastId);
    else if (g_lastId == 0x00)
      snprintf(why, sizeof(why),
               "absent (id=0x00: MISO low — no 3V3 at the module, or MISO not connected)");
    else if (g_lastId == 0xFF)
      snprintf(why, sizeof(why),
               "absent (id=0xFF: MISO floating — powered but never selected, check CS)");
    else
      snprintf(why, sizeof(why), "absent (id=0x%02x: answers, but not an Arducam Mega)",
               (unsigned)g_lastId);
    return std::string(why);
  }

  char buf[96];
  // Capture one frame synchronously (bounded): this is a bench command, and the
  // FIFO length is the number worth seeing — 18432 is a clean 96x96 RGB565 frame.
  startCapture();
  uint32_t deadline = (uint32_t)k_uptime_get() + kCaptureTimeoutMs;
  int st = 0;
  do {
    st = readReg(REG_SENSOR_STATE);
    if (st >= 0 && (st & CAP_DONE)) break;
    k_msleep(5);
  } while ((uint32_t)k_uptime_get() < deadline);

  uint32_t len = 0;
  if (st >= 0 && (st & CAP_DONE)) {
    len = (uint32_t)readReg(REG_FIFO_SIZE1) | ((uint32_t)readReg(REG_FIFO_SIZE2) << 8) |
          ((uint32_t)readReg(REG_FIFO_SIZE3) << 16);
  }
  g_state = kIdle;  // drop this frame; the vision service starts its own
  snprintf(buf, sizeof(buf), "id=0x%02x fw=%u-%u-%u/%02x frame=%u (expect %d)", g_sensorId,
           g_fw[0], g_fw[1], g_fw[2], g_fw[3], (unsigned)len, kPixels * 2);
  return std::string(buf);
}

#else  // no camera node in this board's overlay

int arducam_capture_rgb(uint8_t*, int) { return 0; }
std::string arducam_probe() { return "absent (no camera node in the board overlay)"; }

#endif
