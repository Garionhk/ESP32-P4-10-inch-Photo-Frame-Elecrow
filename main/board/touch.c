#include "touch.h"
#include "board.h"
#include "settings.h"
#include "display.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include <stdlib.h>

static const char *TAG = "touch";

static esp_lcd_touch_handle_t s_tp;
static i2c_master_bus_handle_t s_bus;
static QueueHandle_t s_q;

// --- gesture thresholds ----------------------------------------------------
// A finger on a 10" panel wanders a long way while "not moving", so the tap
// slop is generous; the swipe distance is what actually separates the two.
#define TAP_SLOP_PX      60      // movement still counted as a tap
#define SWIPE_MIN_PX     90      // travel before it is a swipe
#define SWIPE_RATIO      1.4f    // vertical must beat horizontal by this
#define TAP_MAX_MS       500     // longer than this is a hold, not a tap
// 220 ms, not 350. Every single tap is held for this long to see whether a
// second follows, so it is felt as latency on the common gesture. 220 is still
// comfortably above a deliberate double tap and takes a third off the wait.
#define DOUBLE_GAP_MS    220

static bool     s_down;
static int      s_x0, s_y0, s_x, s_y;
static int64_t  s_down_at;
static int64_t  s_last_tap_at;
static bool     s_tap_pending;   // one tap seen, waiting to see if a second follows
static gesture_t touch_poll(void);
static bool     s_trace = false; // raw contact tracing; flip on to debug gestures

const char *gesture_name(gesture_t g)
{
    switch (g) {
        case GESTURE_TAP:        return "tap";
        case GESTURE_DOUBLE_TAP: return "double tap";
        case GESTURE_SWIPE_UP:   return "swipe up";
        case GESTURE_SWIPE_DOWN: return "swipe down";
        default:                 return "none";
    }
}

static void touch_task(void *arg)
{
    (void)arg;
    for (;;) {
        gesture_t g = touch_poll();
        if (g != GESTURE_NONE) xQueueSend(s_q, &g, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

gesture_t touch_take(void)
{
    gesture_t g = GESTURE_NONE;
    if (s_q) xQueueReceive(s_q, &g, 0);
    return g;
}

esp_err_t touch_bus_init(void)
{
    if (s_bus) return ESP_OK;
    i2c_master_bus_config_t bcfg = {
        .i2c_port = BSP_I2C_NUM,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bcfg, &s_bus);
    if (err != ESP_OK) ESP_LOGE(TAG, "i2c bus: %s", esp_err_to_name(err));
    else               ESP_LOGI(TAG, "I2C%d claimed (SDA %d SCL %d)", BSP_I2C_NUM,
                                BSP_I2C_SDA, BSP_I2C_SCL);
    return err;
}

esp_err_t touch_init(void)
{
    i2c_master_bus_handle_t bus = s_bus;
    if (!bus && i2c_master_get_bus_handle(BSP_I2C_NUM, &bus) != ESP_OK) {
        ESP_LOGE(TAG, "no I2C bus -- was touch_bus_init() called?");
        return ESP_FAIL;
    }

    const esp_lcd_touch_config_t base = {
        // Native panel coordinates. Rotation is applied downstream, exactly
        // as it is for photos.
        .x_max = PANEL_H_RES,
        .y_max = PANEL_V_RES,
        .rst_gpio_num = BSP_TOUCH_RST,
        .int_gpio_num = BSP_TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };

    // The GT911 latches its address from INT during reset, so it can come up
    // on either. Try the primary, then the backup -- as Elecrow's example does.
    static const uint8_t addrs[] = {
        ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP,
    };
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (size_t i = 0; i < sizeof addrs && !s_tp; i++) {
        esp_lcd_panel_io_handle_t io = NULL;
        esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        io_cfg.dev_addr = addrs[i];
        // IDF 5.5's new-style I2C made scl_speed_hz REQUIRED; left at zero the
        // panel IO is rejected with nothing saying the clock is why.
        io_cfg.scl_speed_hz = BSP_I2C_CLK_HZ;
        if (esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io) != ESP_OK) continue;

        esp_lcd_touch_config_t cfg = base;
        esp_lcd_touch_io_gt911_config_t gt = { .dev_addr = addrs[i] };
        cfg.driver_data = &gt;
        err = esp_lcd_touch_new_i2c_gt911(io, &cfg, &s_tp);
        if (err != ESP_OK) {
            esp_lcd_panel_io_del(io);
            s_tp = NULL;
        } else {
            ESP_LOGI(TAG, "GT911 ready at 0x%02X", addrs[i]);
        }
    }
    if (!s_tp) {
        ESP_LOGE(TAG, "GT911 not found at 0x5D or 0x14: %s", esp_err_to_name(err));
        return err;
    }

    // A dedicated task so the panel is watched continuously, including while
    // the compositor is busy with a transition. Low priority and a 10 ms
    // period: it must never compete with rendering.
    s_q = xQueueCreate(8, sizeof(gesture_t));
    if (!s_q) return ESP_ERR_NO_MEM;
    // PSRAM stack -- see the note in main.c about internal RAM and the radio.
    xTaskCreatePinnedToCoreWithCaps(touch_task, "touch", 4096, NULL, 2, NULL, 0,
                                    MALLOC_CAP_SPIRAM);
    return ESP_OK;
}

// Map the panel's raw coordinates into VIEW space -- the space the user is
// actually looking at.
//
// The GT911 reports in the panel's own native frame (1024x600, landscape).
// When the view is turned against the panel (display_view_rotated(): PORTRAIT
// on this board) a swipe the user makes upward arrives as movement along a
// panel axis, so it has to be mapped back.
//
// The mapping is the exact INVERSE of the rotation the compositor applies:
// photo.c and overlay.c both use PPA_SRM_ROTATION_ANGLE_90 for a rotated view
// (270 with rotate180), and ppa.h documents those angles as counter-clockwise.
// A 90 deg CCW turn sends view (vx,vy) to panel (vy, VMAX-vx), so:
//
//   not rotated           view = panel
//   not rotated + 180     view = panel mirrored in both axes
//   rotated   (PPA 90)    vx = VMAX - py,  vy = px
//   rotated + 180 (270)   vx = py,         vy = HMAX - px
//
// Cross-check: the overlay band for a rotated view sits at the panel's far x
// edge (display_w - strip height); view bottom maps to px = HMAX there.
//
// HISTORY: the previous board (Guition, GSL3680 touch) had these two rows the
// other way round, MEASURED by swiping, because its touch controller reported
// rotated 180 deg against its own panel. Carried over unchanged, they reversed
// up and down in portrait on this board -- which is how the derivation above
// was checked. The physical way the user turns the device does not enter into
// it: the PPA fixes how view pixels land on the panel, and touch follows that.
static void to_view(int px, int py, int *vx, int *vy)
{
    const int HMAX = PANEL_H_RES - 1, VMAX = PANEL_V_RES - 1;
    if (!display_view_rotated()) {
        *vx = g_settings.rotate180 ? HMAX - px : px;
        *vy = g_settings.rotate180 ? VMAX - py : py;
    } else if (!g_settings.rotate180) {
        *vx = VMAX - py;
        *vy = px;
    } else {
        *vx = py;
        *vy = HMAX - px;
    }
}

static gesture_t touch_poll(void)
{
    if (!s_tp) return GESTURE_NONE;

    const int64_t now = esp_timer_get_time() / 1000;   // ms

    uint16_t xs[1], ys[1];
    uint8_t n = 0;
    esp_err_t rd = esp_lcd_touch_read_data(s_tp);
    const bool got = esp_lcd_touch_get_coordinates(s_tp, xs, ys, NULL, &n, 1);
    const bool pressed = got && n > 0;

    // Raw trace: prints on every CHANGE of contact state, so a press and a
    // release each produce one line and a resting panel produces none.
    // Without this there is no way to tell "the panel reports nothing" from
    // "the gesture thresholds are wrong" -- they look identical from outside.
    if (s_trace) {
        static bool last_pressed;
        static int64_t last_log;
        if (pressed != last_pressed) {
            ESP_LOGI(TAG, "%s  rd=%s got=%d n=%u  x=%u y=%u",
                     pressed ? "DOWN" : "UP  ", esp_err_to_name(rd), got, n,
                     pressed ? xs[0] : 0, pressed ? ys[0] : 0);
            last_pressed = pressed;
        } else if (pressed && (now - last_log) > 120) {
            last_log = now;
            ESP_LOGI(TAG, "MOVE  x=%u y=%u  (dx=%d dy=%d)",
                     xs[0], ys[0], (int)xs[0] - s_x0, (int)ys[0] - s_y0);
        }
    }

    if (pressed) {
        // Everything below works in VIEW space, never the panel's.
        int vx, vy;
        to_view(xs[0], ys[0], &vx, &vy);
        if (!s_down) {                       // finger arrived
            s_down = true;
            s_x0 = s_x = vx;
            s_y0 = s_y = vy;
            s_down_at = now;
        } else {
            s_x = vx;
            s_y = vy;
        }
        return GESTURE_NONE;                 // decide on release, never during
    }

    // A pending single tap becomes a real one once the double-tap window
    // closes. Reporting it immediately would mean every double tap also fired
    // a single tap first, advancing a photo before going back.
    if (!s_down) {
        if (s_tap_pending && (now - s_last_tap_at) > DOUBLE_GAP_MS) {
            s_tap_pending = false;
            return GESTURE_TAP;
        }
        return GESTURE_NONE;
    }

    // --- finger lifted: classify ---
    s_down = false;
    const int dx = s_x - s_x0, dy = s_y - s_y0;
    const int adx = abs(dx), ady = abs(dy);
    const int64_t held = now - s_down_at;

    if (s_trace)
        ESP_LOGI(TAG, "release: dx=%d dy=%d held=%lldms", dx, dy, held);

    if (ady >= SWIPE_MIN_PX && ady > (int)(adx * SWIPE_RATIO)) {
        // A swipe cancels any pending tap: the user is doing something else.
        s_tap_pending = false;
        // Panel Y grows downward, so a positive dy is a downward swipe.
        return (dy > 0) ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
    }

    if (adx <= TAP_SLOP_PX && ady <= TAP_SLOP_PX && held <= TAP_MAX_MS) {
        if (s_tap_pending && (now - s_last_tap_at) <= DOUBLE_GAP_MS) {
            s_tap_pending = false;
            return GESTURE_DOUBLE_TAP;
        }
        s_tap_pending = true;
        s_last_tap_at = now;
        return GESTURE_NONE;                 // wait out the double-tap window
    }

    return GESTURE_NONE;                     // a hold, or a horizontal drag
}
