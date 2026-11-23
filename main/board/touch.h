// ===========================================================================
// touch.h -- GT911 capacitive panel, reduced to gestures
// ===========================================================================
// The panel reports raw points; the frame only cares about four things. All
// gesture decisions are made ON RELEASE, so one press can produce exactly one
// event -- never a tap that also fires as a swipe.
#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    GESTURE_NONE = 0,
    GESTURE_TAP,          // next photo
    GESTURE_DOUBLE_TAP,   // previous photo
    GESTURE_SWIPE_UP,     // show the info panel
    GESTURE_SWIPE_DOWN,   // hide the info panel
} gesture_t;

// Claim the I2C bus (new-style driver). Kept separate from touch_init() so
// anything else on the bus can be brought up before or without touch.
esp_err_t touch_bus_init(void);

// Call after display_init(): the panel rails are up by then.
esp_err_t touch_init(void);

// Take the next recognised gesture, or GESTURE_NONE if there is none waiting.
//
// Polling runs in its own task rather than on the render loop. It has to:
// a transition takes ~700 ms during which the render loop is not looking at
// the panel at all, so the second half of a double tap was simply lost, and a
// tap during a transition did nothing.
gesture_t touch_take(void);

const char *gesture_name(gesture_t g);
