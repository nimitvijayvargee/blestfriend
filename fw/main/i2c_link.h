#pragma once

#include <stdint.h>
#include "esp_err.h"

/*
 * I2C slave command link to the host. See protocol.h for the frame format.
 */
esp_err_t i2c_link_init(void);

/*
 * Queue an event for the host and assert the IRQ line.
 * Safe to call from Bluetooth callback tasks.
 * Data is truncated to 8 bytes.
 */
void i2c_link_post_event(uint8_t id, const uint8_t *data, uint8_t len);
