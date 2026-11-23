// ===========================================================================
// sdcard.h -- microSD on SDMMC slot 0, 1-bit
// ===========================================================================
// 1-bit bus (only D0 is routed on the CrowPanel). The card's rail is internal
// LDO 4, switched on by display_init() -- so mount AFTER the display is up, or
// every attempt times out exactly like a missing card.
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t sdcard_mount(void);
void      sdcard_unmount(void);
bool      sdcard_mounted(void);

// Capacity/free in bytes. Returns false if not mounted.
bool sdcard_usage(uint64_t *total, uint64_t *freeb);
