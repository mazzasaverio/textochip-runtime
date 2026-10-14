#pragma once

#include <stdbool.h>

// BIG BUFFERS IN EXTERNAL RAM, where a board has it.
//
// The ESP32-S3's internal RAM is full (99.7% after contract 3, 2026-10-14):
// the voice window and the MFCC tables move to the module's octal PSRAM (the
// N8R8 module of the kit: zephyr/boards/esp32s3_devkitc_esp32s3_procpu.conf,
// CONFIG_ESP_SPIRAM), which frees about 137 KB for new primitives. Every other
// board, and the host, keeps them where they were.
//
// A board whose PSRAM did not come up (another module, a fault) still boots:
// Zephyr only prints the failure. tc_big_ram_ready() says so, and the voice
// service then stays silent instead of touching unmapped memory.

#if defined(CONFIG_ESP_SPIRAM)
#define TC_BIG_RAM __attribute__((section(".ext_ram.bss")))
#ifdef __cplusplus
extern "C" {
#endif
bool esp_psram_is_initialized(void);
#ifdef __cplusplus
}
#endif
static inline bool tc_big_ram_ready(void) { return esp_psram_is_initialized(); }
#else
#define TC_BIG_RAM
static inline bool tc_big_ram_ready(void) { return true; }
#endif
