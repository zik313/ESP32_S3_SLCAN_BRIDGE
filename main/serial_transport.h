#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

void serial_transport_initialize(void);
bool serial_transport_is_connected(void);
int serial_transport_read_bytes(uint8_t *buffer, size_t buffer_length, TickType_t timeout_ticks);
esp_err_t serial_transport_write_bytes(const uint8_t *buffer, size_t buffer_length, TickType_t timeout_ticks);
esp_err_t serial_transport_write_text(const char *text, TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif
