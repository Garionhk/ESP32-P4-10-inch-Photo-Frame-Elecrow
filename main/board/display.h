// ===========================================================================
// display.h -- EK79007 MIPI-DSI panel, framebuffers, backlight
// ===========================================================================
// Every value this uses lives in board.h with its provenance. The ones that
// produce a black screen when wrong: the DPHY rail (internal LDO 3), the IO
// rail (internal LDO 4), and the backlight (LEDC PWM on GPIO 31).
#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include <stdbool.h>
#include <stdint.h>

// Bring up the LDO rails, the DSI bus, the EK79007 and the backlight.
esp_err_t display_init(void);

// Native panel geometry: 1024x600, LANDSCAPE. A portrait view is a rotation
// applied once per photo in the fit pass, never per frame.
int display_width(void);
int display_height(void);

// True when the user's chosen orientation (g_settings.landscape) differs from
// the panel's native scan, i.e. when view content must be turned 90 degrees
// to reach the panel. Photos, the overlay and touch all key off this one
// function so they cannot disagree.
bool display_view_rotated(void);

// Framebuffers, for the compositor to render into. There are PANEL_NUM_FBS of
// them; index them 0..n-1.
void *display_framebuffer(int index);
int   display_framebuffer_count(void);

// Hand a framebuffer to the panel. Blocks until the DSI has taken it.
esp_err_t display_present(void *fb);

// 0-100, LEDC PWM. 0 is fully off.
esp_err_t display_set_brightness(int percent);
