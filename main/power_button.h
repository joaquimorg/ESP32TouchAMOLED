#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_BTN_NONE = 0,
    POWER_BTN_PRESSED,   /*!< PWRKEY pressed down (negative edge) */
    POWER_BTN_RELEASED,  /*!< PWRKEY released (positive edge) */
} power_btn_event_t;

typedef struct {
    bool battery_present;
    bool vbus_present;
    bool charging;
    int battery_percent; /*!< 0..100, or -1 if unavailable/no battery */
} power_status_t;

/**
 * @brief Initialize access to the AXP2101 PWRKEY (over the shared BSP I2C bus).
 *
 * Enables the power-key edge interrupts so press/release can be polled.
 * The actual power-off on long press is handled by the AXP2101 in hardware.
 */
esp_err_t power_button_init(void);

/**
 * @brief Poll the AXP2101 for a pending power-key event.
 *
 * @return POWER_BTN_PRESSED / POWER_BTN_RELEASED, or POWER_BTN_NONE if nothing pending.
 */
power_btn_event_t power_button_poll(void);

/**
 * @brief Read AXP2101 battery/USB status.
 */
esp_err_t power_status_read(power_status_t *status);

#ifdef __cplusplus
}
#endif
