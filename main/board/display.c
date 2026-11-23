#include "display.h"
#include "board.h"
#include "settings.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_ek79007.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t     s_panel;
static esp_lcd_dsi_bus_handle_t   s_bus;
static esp_lcd_panel_io_handle_t  s_io;
static esp_ldo_channel_handle_t   s_ldo_phy;
static esp_ldo_channel_handle_t   s_ldo_io;
static void                      *s_fb[PANEL_NUM_FBS];
static int                        s_fb_count;
static bool                       s_bl_ready;

bool display_view_rotated(void)
{
    return g_settings.landscape != (bool)PANEL_NATIVE_LANDSCAPE;
}

// Backlight on LEDC so brightness is a real setting. Starts at duty 0: the
// panel is lit only once a black frame is on it.
static esp_err_t backlight_init(void)
{
    // Supply enable first. Harmless if the board already pulls it up.
    gpio_config_t pw = { .pin_bit_mask = 1ULL << BSP_LCD_BL_POWER, .mode = GPIO_MODE_OUTPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&pw), TAG, "bl power gpio");
    gpio_set_level(BSP_LCD_BL_POWER, 1);

    const ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BSP_LCD_BL_DUTY_BITS,
        .timer_num       = BSP_LCD_BL_LEDC_TIMER,
        .freq_hz         = BSP_LCD_BL_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&t), TAG, "ledc timer");

    const ledc_channel_config_t c = {
        .gpio_num   = BSP_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = BSP_LCD_BL_LEDC_CH,
        .timer_sel  = BSP_LCD_BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&c), TAG, "ledc channel");
    s_bl_ready = true;
    return ESP_OK;
}

esp_err_t display_init(void)
{
    // Keep the unused amplifier off (enable is active low) so it cannot hiss.
    gpio_config_t amp = { .pin_bit_mask = 1ULL << BSP_POWER_AMP_IO, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&amp);
    gpio_set_level(BSP_POWER_AMP_IO, 1);

    // Both internal LDOs, exactly as Elecrow's bring-up does. LDO3 feeds the
    // DSI PHY -- skip it and the bus initialises into silence. LDO4 feeds the
    // IO domain shared by the panel and the microSD pins, and stays on for good.
    esp_ldo_channel_config_t phy = { .chan_id = PANEL_MIPI_LDO_CHAN, .voltage_mv = PANEL_MIPI_LDO_MV };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&phy, &s_ldo_phy), TAG, "DPHY LDO");
    esp_ldo_channel_config_t io = { .chan_id = BSP_IO_LDO_CHAN, .voltage_mv = BSP_IO_LDO_MV };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&io, &s_ldo_io), TAG, "IO LDO");

    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id             = 0,
        .num_data_lanes     = PANEL_DSI_LANES,
        .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = PANEL_DSI_LANE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &s_bus), TAG, "dsi bus");

    esp_lcd_dbi_io_config_t io_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_bus, &io_cfg, &s_io), TAG, "dbi io");

    // Two framebuffers: draw the next frame while the DSI scans this one.
    esp_lcd_dpi_panel_config_t dpi = {
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = PANEL_DPI_CLK_MHZ,
        .virtual_channel    = 0,
        .pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs            = PANEL_NUM_FBS,
        .video_timing = {
            .h_size            = PANEL_H_RES,
            .v_size            = PANEL_V_RES,
            .hsync_back_porch  = PANEL_HSYNC_BACK_PORCH,
            .hsync_pulse_width = PANEL_HSYNC_PULSE_WIDTH,
            .hsync_front_porch = PANEL_HSYNC_FRONT_PORCH,
            .vsync_back_porch  = PANEL_VSYNC_BACK_PORCH,
            .vsync_pulse_width = PANEL_VSYNC_PULSE_WIDTH,
            .vsync_front_porch = PANEL_VSYNC_FRONT_PORCH,
        },
    };

    ek79007_vendor_config_t vendor = {
        .mipi_config = { .dsi_bus = s_bus, .dpi_config = &dpi },
    };
    esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order  = PANEL_RGB_ELEMENT_ORDER,
        .bits_per_pixel = 16,
        .vendor_config  = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ek79007(s_io, &dev, &s_panel), TAG, "ek79007");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    // Not every vendor driver implements disp_on_off; the DPI panel is already
    // scanning after init, so a "not supported" here is not a failure.
    esp_err_t on = esp_lcd_panel_disp_on_off(s_panel, true);
    if (on != ESP_OK && on != ESP_ERR_NOT_SUPPORTED)
        ESP_LOGW(TAG, "disp on: %s", esp_err_to_name(on));

    s_fb_count = PANEL_NUM_FBS;
    if (PANEL_NUM_FBS >= 2) {
        ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, &s_fb[0], &s_fb[1]),
                            TAG, "get fb");
    } else {
        ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, &s_fb[0]),
                            TAG, "get fb");
    }

    // Start black rather than on whatever the PSRAM happened to hold.
    for (int i = 0; i < s_fb_count; i++) memset(s_fb[i], 0, PANEL_FB_BYTES);
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, PANEL_H_RES, PANEL_V_RES, s_fb[0]);

    display_set_brightness(100);

    ESP_LOGI(TAG, "EK79007 %dx%d up, %d framebuffer(s) of %.2f MiB, backlight PWM GPIO %d",
             PANEL_H_RES, PANEL_V_RES, s_fb_count, PANEL_FB_BYTES / 1048576.0,
             BSP_LCD_BACKLIGHT);
    return ESP_OK;
}

int   display_width(void)            { return PANEL_H_RES; }
int   display_height(void)           { return PANEL_V_RES; }
int   display_framebuffer_count(void){ return s_fb_count; }
void *display_framebuffer(int i)     { return (i >= 0 && i < s_fb_count) ? s_fb[i] : NULL; }

esp_err_t display_present(void *fb)
{
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, PANEL_H_RES, PANEL_V_RES, fb);
}

esp_err_t display_set_brightness(int percent)
{
    if (!s_bl_ready) return ESP_ERR_INVALID_STATE;
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    // Elecrow's curve: 0 is off, anything else gets a floor so it stays lit.
    const uint32_t max  = (1u << BSP_LCD_BL_DUTY_BITS) - 1;
    uint32_t duty = percent ? (uint32_t)percent * 18 + 200 : 0;
    if (duty > max) duty = max;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, BSP_LCD_BL_LEDC_CH, duty), TAG, "duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, BSP_LCD_BL_LEDC_CH);
}
