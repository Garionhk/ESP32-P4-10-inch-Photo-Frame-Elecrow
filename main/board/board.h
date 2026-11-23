// ===========================================================================
// board.h -- THE pin map and hardware profile
// ===========================================================================
// Target: Elecrow CrowPanel Advanced 10.1" ESP32-P4 HMI (board rev V1.2)
//   ESP32-P4NRW32 (RISC-V dual core, chip rev v1.x) + ESP32-C6 (WiFi 6 / BLE 5)
//   16 MB NOR flash, 32 MB in-package PSRAM
//   10.1" EK79007 MIPI-DSI IPS panel, 1024x600 native LANDSCAPE, 2 data lanes
//   GT911 capacitive touch, NS4168 I2S amp, PDM mic, microSD
//
// History: this firmware was first brought up on a Guition JC8012P4A1
// (JD9365 800x1280 portrait panel, GSL3680 touch). Those values are in git
// history / pre-elecrow-port-backup.tar.gz. Almost nothing carried over except
// the SoC-fixed SDMMC slot-0 pins and the C6's SDIO slot.
//
// PROVENANCE -- every value below is one of three things:
//
//   [VENDOR]  Read out of Elecrow's own V1.2 ESP-IDF examples
//             (github.com/Elecrow-RD/CrowPanel-Advanced-10.1inch-ESP32-P4-
//             HMI-AI-Display-1024x600-IPS-Touch-Screen, example/V1.2/idf-code):
//             Lesson07 (panel, backlight, LDOs), Lesson05 (touch),
//             Lesson08 (SD), Lesson17 (esp_hosted sdkconfig).
//
//   [WIKI]    Elecrow wiki / community-tested ESPHome config for this board,
//             not present in a vendor example. Plausible, less certain.
//
//   [SOC]     Fixed by the ESP32-P4 silicon; cannot differ per board.
//
//   [STAGE0]  NOT yet confirmed on this board. A hypothesis with a reason.
//
// Nothing below has been measured on THIS board yet. The vendor values come
// from code that ships working, which is the best starting point there is --
// but the last board taught us a vendor page can be wrong (flash size), so the
// first boot is still a gate.
#pragma once

#include "driver/gpio.h"

// ---- Board identity -------------------------------------------------------
#define BOARD_NAME              "Elecrow CrowPanel Advanced 10.1 (ESP32-P4)"

// ---- Panel: EK79007, 1024x600 native landscape ----------------------------
// [VENDOR] Lesson07 bsp_illuminate.c. Driver: espressif/esp_lcd_ek79007.
//
// The panel scans LANDSCAPE -- the opposite of the previous board. The
// orientation logic is written against PANEL_NATIVE_LANDSCAPE, so "landscape"
// in settings means what the user sees, and only a view that disagrees with
// the panel's native scan pays for a rotation (once per photo, in the fit pass).
#define PANEL_H_RES             1024    // [VENDOR] native, landscape
#define PANEL_V_RES             600     // [VENDOR]
#define PANEL_NATIVE_LANDSCAPE  1
#define PANEL_DSI_LANES         2       // [VENDOR]
#define PANEL_DSI_LANE_MBPS     900     // [VENDOR]
#define PANEL_DPI_CLK_MHZ       51      // [VENDOR]

// [VENDOR] video timings. Refresh = clk / h_total / v_total
//          = 51e6 / (1024+160+70+160) / (600+23+10+12) = 51e6/1414/645 = 55.9 Hz
#define PANEL_HSYNC_BACK_PORCH  160
#define PANEL_HSYNC_PULSE_WIDTH 70
#define PANEL_HSYNC_FRONT_PORCH 160
#define PANEL_VSYNC_BACK_PORCH  23
#define PANEL_VSYNC_PULSE_WIDTH 10
#define PANEL_VSYNC_FRONT_PORCH 12

// [VENDOR] Elecrow's bring-up acquires BOTH internal LDOs before the panel:
//   LDO_VO3 = 2.5 V  -> VDD_MIPI_DPHY
//   LDO_VO4 = 3.3 V  -> the IO domain the panel analogue side and the microSD
//                       pins (GPIO 39..44) sit in.
// Channel 4 is held fixed for the life of the firmware by display_init(); the
// SD driver must therefore NOT try to acquire it as an adjustable supply.
#define PANEL_MIPI_LDO_CHAN     3
#define PANEL_MIPI_LDO_MV       2500
#define BSP_IO_LDO_CHAN         4
#define BSP_IO_LDO_MV           3300

// Framebuffers. 1024*600*2 = 1.17 MiB each; two for tear-free compositing.
#define PANEL_NUM_FBS           2
#define PANEL_FB_BYTES          (PANEL_H_RES * PANEL_V_RES * 2)

// ---- Display control pins -------------------------------------------------
// [VENDOR] Elecrow passes reset_gpio_num = -1 and resets the EK79007 by
// command. [WIKI] lists GPIO 41 as LCD reset; kept here, unused, so a panel
// that will not wake has an obvious next thing to try.
#define BSP_LCD_RST             GPIO_NUM_NC
#define BSP_LCD_RST_ALT         GPIO_NUM_41

// [VENDOR] Backlight is LEDC PWM on GPIO 31, 30 kHz, 11-bit, active high.
// Elecrow maps brightness b (1..100) to duty b*18+200 of 2047 -- a floor that
// keeps low settings visibly lit -- and 0 to fully off.
#define BSP_LCD_BACKLIGHT       GPIO_NUM_31
#define BSP_LCD_BL_LEDC_TIMER   0
#define BSP_LCD_BL_LEDC_CH      0
#define BSP_LCD_BL_FREQ_HZ      30000
#define BSP_LCD_BL_DUTY_BITS    11

// [WIKI] "LCD_BK_POWER" -- backlight supply enable. Elecrow's own example never
// drives it, so it is probably pulled up on the board; driving it high is
// belt-and-braces. [STAGE0] if the screen is black with a healthy DSI log,
// check this pin first.
#define BSP_LCD_BL_POWER        GPIO_NUM_29

// Both values measured on the previous board; Elecrow uses RGB order too.
#define PANEL_RGB_ELEMENT_ORDER LCD_RGB_ELEMENT_ORDER_RGB
#define PANEL_NEEDS_INVERSION   0       // [STAGE0]

// ---- I2C: GT911 touch -----------------------------------------------------
// [VENDOR] Lesson05: SDA 45, SCL 46, 400 kHz. Elecrow uses port 0; the port
// number is ours to choose. The board has its own pull-ups [WIKI].
#define BSP_I2C_SCL             GPIO_NUM_46
#define BSP_I2C_SDA             GPIO_NUM_45
#define BSP_I2C_NUM             0
#define BSP_I2C_CLK_HZ          400000

// [VENDOR] GT911. Its address is latched from the INT level during reset:
// 0x5D (INT low) or 0x14 (INT high). touch.c tries both, as Elecrow's does.
#define BSP_TOUCH_RST           GPIO_NUM_40
#define BSP_TOUCH_INT           GPIO_NUM_42

// ---- microSD: SDMMC slot 0, 1-bit ------------------------------------------
// [SOC] slot-0 pins are fixed in silicon. [VENDOR] Lesson08 runs the card in
// 1-bit mode: only D0 is routed. CS/D3 is tied to GND on this board, so 4-bit
// is not available -- do not "fix" the width.
#define BSP_SD_CLK              GPIO_NUM_43
#define BSP_SD_CMD              GPIO_NUM_44
#define BSP_SD_D0               GPIO_NUM_39
#define BSP_SD_SLOT             0
#define BSP_SD_WIDTH            1
// Elecrow caps the clock at 10 MHz. 20 MHz (the SD default speed) is a
// [STAGE0] hypothesis; drop to 10000 if mounts or reads are flaky.
#define BSP_SD_MAX_KHZ          20000
#define BSP_SD_MOUNT            "/sdcard"

// ---- Audio (present, unused by this firmware) -----------------------------
// [VENDOR README] NS4168 I2S amp + PDM mic. Kept so the pins read as taken.
#define BSP_I2S_LCLK            GPIO_NUM_21
#define BSP_I2S_SCLK            GPIO_NUM_22
#define BSP_I2S_DOUT            GPIO_NUM_23
#define BSP_MIC_CLK             GPIO_NUM_24
#define BSP_MIC_DATA            GPIO_NUM_26
// [WIKI] amp enable, ACTIVE LOW. display_init() drives it high so an unused,
// floating amplifier cannot hiss.
#define BSP_POWER_AMP_IO        GPIO_NUM_30

// ---- Imaging pipeline limits ----------------------------------------------
// Unchanged from the previous board: these are facts about the P4, not about
// the panel. The HW JPEG decoder cannot downscale, which forces the cache.
#define JPEG_HW_MAX_DIM         2048

// Photos are cached fitted into a square box. 1280 still covers the 1024 px
// long edge here with room to spare, and keeps existing caches valid.
#define CACHE_FIT_BOX           1280

// ---- ESP32-C6 WiFi co-processor (esp_hosted over SDIO) --------------------
// [VENDOR] Lesson17 ESP32_P4-station/sdkconfig. Slot 1, 4-bit, 40 MHz.
//
// NOTE the data-line order: from board rev V1.1 Elecrow REVERSED D0..D3
// relative to Espressif's defaults (D0=17 ... D3=14). A V1.0 board uses
// 14/15/16/17 and will not get WiFi with this map.
//
// Recorded here for documentation; the driver is configured through Kconfig
// (CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_*_SLOT_1 in sdkconfig.defaults -- esp_hosted
// is pinned to 2.12.3 to match the C6's firmware, and those are 2.x names).
#define BSP_C6_SDIO_SLOT        1
#define BSP_C6_SDIO_CLK         18
#define BSP_C6_SDIO_CMD         19
#define BSP_C6_SDIO_D0          17
#define BSP_C6_SDIO_D1          16
#define BSP_C6_SDIO_D2          15
#define BSP_C6_SDIO_D3          14
// CONFIRMED on hardware 2026-11-23: the C6 reset is GPIO 32, active high --
// NOT the 54 in Elecrow's own Lesson17 sdkconfig. With 54 the C6 is never
// actually reset, so it never sends the boot-up event an esp_hosted 2.x host
// waits for: "Not able to connect with ESP-Hosted slave device", then the host
// reboots itself in a loop. A community ESPHome config had 32 all along.
#define BSP_C6_RESET_GPIO       32
#define BSP_C6_SDIO_KHZ         40000
