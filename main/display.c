#include "display.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display";

/* Map Kconfig SPI host number to SPI host enum */
#if CONFIG_BROWSER_LCD_SPI_HOST == 1
#define LCD_HOST       SPI2_HOST
#elif CONFIG_BROWSER_LCD_SPI_HOST == 2
#define LCD_HOST       SPI3_HOST
#else
#define LCD_HOST       SPI2_HOST
#endif
#define PIN_CLK        CONFIG_BROWSER_LCD_PIN_CLK
#define PIN_MOSI       CONFIG_BROWSER_LCD_PIN_MOSI
#define PIN_MISO       CONFIG_BROWSER_LCD_PIN_MISO
#define PIN_DC         CONFIG_BROWSER_LCD_PIN_DC
#define PIN_CS         CONFIG_BROWSER_LCD_PIN_CS
#define PIN_RST        CONFIG_BROWSER_LCD_PIN_RST
#define PIN_BL         CONFIG_BROWSER_LCD_PIN_BL
#define LCD_H_RES      CONFIG_BROWSER_LCD_WIDTH
#define LCD_V_RES      CONFIG_BROWSER_LCD_HEIGHT

/* ST7796S commands */
#define ST7796S_CMD_SWRESET     0x01
#define ST7796S_CMD_SLPIN       0x10
#define ST7796S_CMD_SLPOUT      0x11
#define ST7796S_CMD_INVOFF      0x20
#define ST7796S_CMD_INVON       0x21
#define ST7796S_CMD_DISPOFF     0x28
#define ST7796S_CMD_DISPON      0x29
#define ST7796S_CMD_CASET       0x2A
#define ST7796S_CMD_RASET       0x2B
#define ST7796S_CMD_RAMWR       0x2C
#define ST7796S_CMD_MADCTL      0x36
#define ST7796S_CMD_COLMOD      0x3A
#define ST7796S_CMD_PORCTRL     0xB2
#define ST7796S_CMD_GCTRL       0xB7
#define ST7796S_CMD_VCOMS       0xBB
#define ST7796S_CMD_SEQCTRL     0xC0
#define ST7796S_CMD_FRCTRL2     0xC6
#define ST7796S_CMD_PVGAMCTRL   0xE0
#define ST7796S_CMD_NVGAMCTRL   0xE1

/* LVGL display buffers - allocated in PSRAM */
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_buf1 = NULL;
static lv_color_t *s_buf2 = NULL;

/* LCD panel handle */
static esp_lcd_panel_handle_t s_panel = NULL;

/* ---- LVGL flush callback ---- */

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1,
                              color_p);
    lv_disp_flush_ready(drv);
}

/* ---- ST7796S initialization sequence ---- */

static esp_err_t st7796s_init(esp_lcd_panel_io_handle_t io)
{
    /* Software reset */
    esp_lcd_panel_io_tx_param(io, ST7796S_CMD_SWRESET, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Sleep out */
    esp_lcd_panel_io_tx_param(io, ST7796S_CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Color mode: 16-bit RGB565 */
    uint8_t colmod = 0x55;
    esp_lcd_panel_io_tx_param(io, ST7796S_CMD_COLMOD, &colmod, 1);

    /* Memory Data Access Control (MADCTL)
     * Bits: MY(7) MX(6) MW(5) ML(4) RGB/BGR(3) - rotate/flip/color order
     * We handle rotation fully here so we must NOT call esp_lcd_panel_mirror()
     * afterwards, as it would send another MADCTL and overwrite these settings.
     *
     * ST7796S MADCTL bits:
     *   Bit 7 (MY)  - Row address order
     *   Bit 6 (MX)  - Column address order
     *   Bit 5 (MW)  - Row/Column exchange
     *   Bit 3       - RGB/BGR order (set = BGR)
     */
    uint8_t madctl = 0x00;
#if CONFIG_BROWSER_LCD_ROTATION == 0
    madctl = 0x00;  /* Portrait normal: RGB, no flip */
#elif CONFIG_BROWSER_LCD_ROTATION == 90
    madctl = 0x60;  /* Landscape: MX + MW set */
#elif CONFIG_BROWSER_LCD_ROTATION == 180
    madctl = 0xC0;  /* Portrait flipped: MY + MX set */
#elif CONFIG_BROWSER_LCD_ROTATION == 270
    madctl = 0xA0;  /* Landscape flipped: MY + MW set */
#endif
    esp_lcd_panel_io_tx_param(io, ST7796S_CMD_MADCTL, &madctl, 1);

    /* Porch setting */
    uint8_t porctrl[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    esp_lcd_panel_io_tx_param(io, ST7796S_CMD_PORCTRL, porctrl, sizeof(porctrl));

    /* Frame rate control - 60Hz */
    uint8_t frctrl[] = {0x00};
    esp_lcd_panel_io_tx_param(io, ST7796S_CMD_FRCTRL2, frctrl, sizeof(frctrl));

    /* Power control 1 - VGH/VDV */
    uint8_t pwctrl1[] = {0x36, 0x36};
    esp_lcd_panel_io_tx_param(io, ST7796S_CMD_SEQCTRL, pwctrl1, sizeof(pwctrl1));

    /* Power control 2 - VAP/VAN */
    uint8_t pwctrl2[] = {0x35};
    esp_lcd_panel_io_tx_param(io, 0xC1, pwctrl2, sizeof(pwctrl2));

    /* VCOM setting */
    uint8_t vcoms[] = {0x3F};
    esp_lcd_panel_io_tx_param(io, ST7796S_CMD_VCOMS, vcoms, sizeof(vcoms));

    /* Gate control */
    uint8_t gctrl[] = {0x35};
    esp_lcd_panel_io_tx_param(io, ST7796S_CMD_GCTRL, gctrl, sizeof(gctrl));

    /* Positive gamma */
    uint8_t pgamma[] = {0x0F, 0x22, 0x1C, 0x1B, 0x08, 0x0F, 0x48, 0xB8,
                        0x34, 0x07, 0x13, 0x0E, 0x29, 0x2E, 0x38};
    esp_lcd_panel_io_tx_param(io, ST7796S_CMD_PVGAMCTRL, pgamma, sizeof(pgamma));

    /* Negative gamma */
    uint8_t ngamma[] = {0x10, 0x1E, 0x01, 0x0C, 0x07, 0x05, 0x36, 0x44,
                        0x4C, 0x37, 0x0F, 0x0F, 0x2B, 0x2D, 0x38};
    esp_lcd_panel_io_tx_param(io, ST7796S_CMD_NVGAMCTRL, ngamma, sizeof(ngamma));

    /* Display inversion on (compensates for LCD polarity) */
    esp_lcd_panel_io_tx_param(io, ST7796S_CMD_INVON, NULL, 0);

    /* Display on */
    esp_lcd_panel_io_tx_param(io, ST7796S_CMD_DISPON, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    return ESP_OK;
}

/* ---- Hardware reset via GPIO ---- */

static void hw_reset(void)
{
    if (PIN_RST >= 0) {
        gpio_config_t rst_cfg = {
            .pin_bit_mask = BIT64(PIN_RST),
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&rst_cfg);
        gpio_set_level(PIN_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(PIN_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

/* ---- Public API ---- */

lv_disp_t *display_init(void)
{
    ESP_LOGI(TAG, "Initializing ST7796S display (%dx%d)", LCD_H_RES, LCD_V_RES);

    /* 1. Initialize SPI bus */
    spi_bus_config_t buscfg = {
        .sclk_io_num     = PIN_CLK,
        .mosi_io_num     = PIN_MOSI,
        .miso_io_num     = PIN_MISO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(lv_color_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI bus initialized");

    /* 2. Create LCD panel IO (SPI) */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num         = PIN_DC,
        .cs_gpio_num         = PIN_CS,
        .pclk_hz             = 40 * 1000 * 1000,  /* 40MHz */
        .lcd_cmd_bits        = 8,
        .lcd_param_bits      = 8,
        .spi_mode            = 0,
        .trans_queue_depth   = 10,
        .on_color_trans_done = NULL,
        .user_ctx            = NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    /* 3. Hardware reset the display */
    hw_reset();

    /* 4. Send custom ST7796S init sequence
     * We do NOT use esp_lcd_panel_init() because the ST7789 driver's
     * default init would conflict with our ST7796S-specific sequence.
     * Our init handles: reset, config, gamma, rotation (MADCTL), and display-on.
     * We also do NOT call esp_lcd_panel_mirror() because it would send a
     * second MADCTL command that overwrites our rotation settings. */
    ESP_ERROR_CHECK(st7796s_init(io_handle));

    /* 5. Create panel handle for draw_bitmap support
     * ST7796S is register-compatible with ST7789 for the basic
     * CASET/RASET/RAMWR drawing commands, so we can use the ST7789 driver.
     * We set reset_gpio_num to -1 since we already did HW reset manually.
     * We skip esp_lcd_panel_init() to avoid ST7789 init commands. */
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,    /* Already reset manually */
        .color_space    = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel));

    /* 6. Backlight on */
    if (PIN_BL >= 0) {
        gpio_config_t bl_cfg = {
            .pin_bit_mask = BIT64(PIN_BL),
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&bl_cfg);
        gpio_set_level(PIN_BL, 1);
    }

    /* 7. Allocate LVGL draw buffers in PSRAM */
    int buf_size = LCD_H_RES * 20;  /* 20 lines per buffer */
    s_buf1 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_buf2 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(s_buf1 && s_buf2);

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buf_size);

    /* 8. Create LVGL display driver */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = LCD_H_RES;
    disp_drv.ver_res  = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &s_draw_buf;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Display initialized successfully");
    return disp;
}

int display_get_h_res(void)
{
    return LCD_H_RES;
}

int display_get_v_res(void)
{
    return LCD_V_RES;
}
