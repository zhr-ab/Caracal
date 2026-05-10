#include "touch.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "touch";

#define TOUCH_SDA     CONFIG_BROWSER_TOUCH_SDA
#define TOUCH_SCL     CONFIG_BROWSER_TOUCH_SCL
#define TOUCH_INT     CONFIG_BROWSER_TOUCH_INT
#define TOUCH_ADDR    CONFIG_BROWSER_TOUCH_ADDR
#define I2C_PORT      I2C_NUM_0

/* FT6336/FT6x36 register map (also works with CST816S at 0x38) */
#define FT6X36_REG_GESTURE     0x01
#define FT6X36_REG_NUM_TOUCH   0x02
#define FT6X36_REG_TH_TOUCH    0x80
#define FT6X36_REG_XH          0x03
#define FT6X36_REG_XL          0x04
#define FT6X36_REG_YH          0x05
#define FT6X36_REG_YL          0x06

/* Touch data */
typedef struct {
    int16_t x;
    int16_t y;
    bool pressed;
} touch_data_t;

static touch_data_t s_touch_data = {0};

/* ---- I2C helpers ---- */

static esp_err_t i2c_write(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TOUCH_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_read(uint8_t reg, uint8_t *buf, int len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TOUCH_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TOUCH_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read(cmd, buf + len - 1, 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* ---- Read touch point ---- */

static bool touch_read_point(int16_t *x, int16_t *y)
{
    uint8_t data[4] = {0};
    uint8_t num_touch = 0;

    /* Read number of touches */
    if (i2c_read(FT6X36_REG_NUM_TOUCH, &num_touch, 1) != ESP_OK) {
        return false;
    }

    if (num_touch == 0) {
        return false;
    }

    /* Read X and Y coordinates */
    if (i2c_read(FT6X36_REG_XH, data, 4) != ESP_OK) {
        return false;
    }

    /* FT6x36 format:
     * data[0] = XH[7:4] = event flag, XH[3:0] = X[11:8]
     * data[1] = XL[7:0] = X[7:0]
     * data[2] = YH[7:4] = Touch ID,  YH[3:0] = Y[11:8]
     * data[3] = YL[7:0] = Y[7:0]
     */
    *x = ((data[0] & 0x0F) << 8) | data[1];
    *y = ((data[2] & 0x0F) << 8) | data[3];

    return true;
}

/* ---- LVGL touch driver callback ---- */

static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    int16_t x, y;
    if (touch_read_point(&x, &y)) {
        s_touch_data.x = x;
        s_touch_data.y = y;
        s_touch_data.pressed = true;
    } else {
        s_touch_data.pressed = false;
    }

    data->point.x = s_touch_data.x;
    data->point.y = s_touch_data.y;
    data->state = s_touch_data.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* ---- Public API ---- */

lv_indev_t *touch_init(void)
{
    ESP_LOGI(TAG, "Initializing touch controller (addr=0x%02X)", TOUCH_ADDR);

    /* Initialize I2C master */
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_SDA,
        .scl_io_num = TOUCH_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,  /* 400kHz */
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));

    /* Configure interrupt pin (optional - used for wakeup) */
    if (TOUCH_INT >= 0) {
        gpio_config_t int_cfg = {
            .pin_bit_mask = BIT64(TOUCH_INT),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&int_cfg);
    }

    /* Set touch threshold for FT6x36 */
    i2c_write(FT6X36_REG_TH_TOUCH, 22);

    /* Test communication */
    uint8_t val = 0;
    if (i2c_read(FT6X36_REG_GESTURE, &val, 1) != ESP_OK) {
        ESP_LOGW(TAG, "Touch controller not responding at 0x%02X", TOUCH_ADDR);
    } else {
        ESP_LOGI(TAG, "Touch controller detected");
    }

    /* Create LVGL input device */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_read_cb;
    lv_indev_t *indev = lv_indev_drv_register(&indev_drv);

    ESP_LOGI(TAG, "Touch initialized");
    return indev;
}
