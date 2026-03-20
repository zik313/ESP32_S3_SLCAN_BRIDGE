#include "serial_transport.h"
#include "slcan_bridge_project_configuration.h"

#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t write_mutex = NULL;
static bool transport_initialized = false;

void serial_transport_initialize(void)
{
    if (transport_initialized) {
        return;
    }

    usb_serial_jtag_driver_config_t driver_configuration = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    driver_configuration.tx_buffer_size = SLCAN_BRIDGE_USB_TRANSMIT_BUFFER_SIZE_BYTES;
    driver_configuration.rx_buffer_size = SLCAN_BRIDGE_USB_RECEIVE_BUFFER_SIZE_BYTES;

    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&driver_configuration));

    write_mutex = xSemaphoreCreateMutex();
    configASSERT(write_mutex != NULL);

    transport_initialized = true;
}

bool serial_transport_is_connected(void)
{
    return transport_initialized && usb_serial_jtag_is_connected();
}

int serial_transport_read_bytes(uint8_t *buffer, size_t buffer_length, TickType_t timeout_ticks)
{
    if (!transport_initialized || buffer == NULL || buffer_length == 0U) {
        return 0;
    }

    return usb_serial_jtag_read_bytes(buffer, (uint32_t)buffer_length, timeout_ticks);
}

esp_err_t serial_transport_write_bytes(const uint8_t *buffer, size_t buffer_length,
                                        TickType_t timeout_ticks)
{
    if (!transport_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (buffer == NULL || buffer_length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!usb_serial_jtag_is_connected()) {
        return ESP_ERR_NOT_FOUND;
    }

    xSemaphoreTake(write_mutex, portMAX_DELAY);

    size_t total_bytes_written = 0U;
    while (total_bytes_written < buffer_length) {
        const int bytes_written = usb_serial_jtag_write_bytes(
            buffer + total_bytes_written,
            buffer_length - total_bytes_written,
            timeout_ticks);

        if (bytes_written <= 0) {
            xSemaphoreGive(write_mutex);
            return ESP_ERR_TIMEOUT;
        }

        total_bytes_written += (size_t)bytes_written;
    }

    const esp_err_t flush_result = usb_serial_jtag_wait_tx_done(timeout_ticks);
    xSemaphoreGive(write_mutex);
    return flush_result;
}

esp_err_t serial_transport_write_text(const char *text, TickType_t timeout_ticks)
{
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return serial_transport_write_bytes((const uint8_t *)text, strlen(text), timeout_ticks);
}
