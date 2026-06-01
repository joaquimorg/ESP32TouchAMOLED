#include "power_button.h"
#include "driver/i2c_master.h"
#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_log.h"

/* AXP2101 power-management IC, accessed over the shared BSP I2C bus. */
#define AXP2101_ADDR              0x34

#define AXP2101_REG_STATUS1       0x00
#define AXP2101_REG_STATUS2       0x01
#define AXP2101_REG_INTEN2        0x41  /* IRQ enable  bank 2 */
#define AXP2101_REG_INTSTS1       0x48  /* IRQ status bank 1 */
#define AXP2101_REG_INTSTS2       0x49  /* IRQ status bank 2 (PWRKEY edges) */
#define AXP2101_REG_INTSTS3       0x4A  /* IRQ status bank 3 */
#define AXP2101_REG_BAT_PERCENT   0xA4

/* Bits inside INTEN2 / INTSTS2 (bank 2). PWRKEY idles high and is pulled low
 * when pressed, so the falling/negative edge is the press. */
#define AXP2101_PKEY_POS_BIT      (1 << 0)  /* positive edge -> release */
#define AXP2101_PKEY_NEG_BIT      (1 << 1)  /* negative edge -> press   */

static const char *TAG = "power_btn";
static i2c_master_dev_handle_t s_dev = NULL;

static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, pdMS_TO_TICKS(100));
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, pdMS_TO_TICKS(100));
}

esp_err_t power_button_init(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c init failed");

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_STATE, TAG, "no i2c bus");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = AXP2101_ADDR,
        .scl_speed_hz    = CONFIG_BSP_I2C_CLK_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "add AXP2101 failed");

    /* Clear any stale interrupt status. */
    reg_write(AXP2101_REG_INTSTS1, 0xFF);
    reg_write(AXP2101_REG_INTSTS2, 0xFF);
    reg_write(AXP2101_REG_INTSTS3, 0xFF);

    /* Enable PWRKEY press (negative) and release (positive) edge interrupts. */
    uint8_t inten2 = 0;
    reg_read(AXP2101_REG_INTEN2, &inten2);
    inten2 |= (AXP2101_PKEY_POS_BIT | AXP2101_PKEY_NEG_BIT);
    ESP_RETURN_ON_ERROR(reg_write(AXP2101_REG_INTEN2, inten2), TAG, "enable PWRKEY irq failed");

    ESP_LOGI(TAG, "AXP2101 PWRKEY monitoring started");
    return ESP_OK;
}

power_btn_event_t power_button_poll(void)
{
    uint8_t sts = 0;
    if (reg_read(AXP2101_REG_INTSTS2, &sts) != ESP_OK) {
        return POWER_BTN_NONE;
    }

    power_btn_event_t ev = POWER_BTN_NONE;
    if (sts & AXP2101_PKEY_NEG_BIT) {
        ev = POWER_BTN_PRESSED;
    } else if (sts & AXP2101_PKEY_POS_BIT) {
        ev = POWER_BTN_RELEASED;
    }

    /* Clear the bits we just handled (write 1 to clear). */
    if (sts & (AXP2101_PKEY_POS_BIT | AXP2101_PKEY_NEG_BIT)) {
        reg_write(AXP2101_REG_INTSTS2, sts);
    }
    return ev;
}

esp_err_t power_status_read(power_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is NULL");

    uint8_t status1 = 0;
    uint8_t status2 = 0;
    ESP_RETURN_ON_ERROR(reg_read(AXP2101_REG_STATUS1, &status1), TAG, "read status1 failed");
    ESP_RETURN_ON_ERROR(reg_read(AXP2101_REG_STATUS2, &status2), TAG, "read status2 failed");

    status->battery_present = (status1 & (1u << 3)) != 0;
    status->vbus_present = ((status2 & (1u << 3)) == 0) && ((status1 & (1u << 5)) != 0);
    status->charging = (status2 >> 5) == 0x01;
    status->battery_percent = -1;

    if (status->battery_present) {
        uint8_t percent = 0;
        ESP_RETURN_ON_ERROR(reg_read(AXP2101_REG_BAT_PERCENT, &percent), TAG, "read battery percent failed");
        status->battery_percent = percent > 100 ? 100 : percent;
    }

    return ESP_OK;
}
