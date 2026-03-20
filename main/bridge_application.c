#include "bridge_application.h"
#include "slcan_bridge_project_configuration.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "can_bus_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "serial_transport.h"
#include "slcan_command_assembler.h"
#include "slcan_protocol.h"

typedef struct {
    bool              timestamps_enabled;
    SemaphoreHandle_t settings_mutex;
} bridge_runtime_state_t;

static bridge_runtime_state_t runtime_state = {
    .timestamps_enabled = false,
    .settings_mutex     = NULL,
};

static bool get_timestamps_enabled(void)
{
    xSemaphoreTake(runtime_state.settings_mutex, portMAX_DELAY);
    const bool timestamps_enabled = runtime_state.timestamps_enabled;
    xSemaphoreGive(runtime_state.settings_mutex);
    return timestamps_enabled;
}

static void set_timestamps_enabled(bool timestamps_enabled)
{
    xSemaphoreTake(runtime_state.settings_mutex, portMAX_DELAY);
    runtime_state.timestamps_enabled = timestamps_enabled;
    xSemaphoreGive(runtime_state.settings_mutex);
}

static void send_protocol_text(const char *text)
{
    (void)serial_transport_write_text(text, pdMS_TO_TICKS(100));
}

static void send_protocol_buffer(const char *buffer, size_t length)
{
    if (length == 0U) {
        return;
    }
    (void)serial_transport_write_bytes((const uint8_t *)buffer, length, pdMS_TO_TICKS(100));
}

static void handle_parsed_command(const slcan_command_t *command)
{
    if (command == NULL) {
        send_protocol_text(slcan_protocol_get_error_reply());
        return;
    }

    char response_buffer[40] = {0};

    switch (command->type) {
        case SLCAN_COMMAND_TYPE_OPEN_NORMAL:
            if (can_bus_service_start(CAN_BUS_OPERATING_MODE_NORMAL) == ESP_OK) {
                send_protocol_text(slcan_protocol_get_success_reply());
            } else {
                send_protocol_text(slcan_protocol_get_error_reply());
            }
            return;

        case SLCAN_COMMAND_TYPE_OPEN_LISTEN_ONLY:
            if (can_bus_service_start(CAN_BUS_OPERATING_MODE_LISTEN_ONLY) == ESP_OK) {
                send_protocol_text(slcan_protocol_get_success_reply());
            } else {
                send_protocol_text(slcan_protocol_get_error_reply());
            }
            return;

        case SLCAN_COMMAND_TYPE_CLOSE:
            if (can_bus_service_stop() == ESP_OK) {
                send_protocol_text(slcan_protocol_get_success_reply());
            } else {
                send_protocol_text(slcan_protocol_get_error_reply());
            }
            return;

        case SLCAN_COMMAND_TYPE_SET_BITRATE:
            if (can_bus_service_set_speed_from_slcan_code(command->speed_code) == ESP_OK) {
                send_protocol_text(slcan_protocol_get_success_reply());
            } else {
                send_protocol_text(slcan_protocol_get_error_reply());
            }
            return;

        case SLCAN_COMMAND_TYPE_READ_STATUS_FLAGS: {
            /* LAWICEL reference: команда F активна только при открытом канале.
               Закрытый канал → BELL, как и для команд t/T/r/R. */
            const can_bus_status_snapshot_t snapshot = can_bus_service_get_status_snapshot();
            if (!snapshot.bus_running) {
                send_protocol_text(slcan_protocol_get_error_reply());
                return;
            }
            const size_t response_length = slcan_protocol_encode_status_flags(
                snapshot.slcan_status_flags,
                response_buffer,
                sizeof(response_buffer));
            send_protocol_buffer(response_buffer, response_length);
            return;
        }

        case SLCAN_COMMAND_TYPE_GET_FIRMWARE_VERSION: {
            const size_t response_length = slcan_protocol_encode_firmware_version(
                response_buffer,
                sizeof(response_buffer));
            send_protocol_buffer(response_buffer, response_length);
            return;
        }

        case SLCAN_COMMAND_TYPE_GET_SERIAL_NUMBER: {
            const size_t response_length = slcan_protocol_encode_serial_number(
                response_buffer,
                sizeof(response_buffer));
            send_protocol_buffer(response_buffer, response_length);
            return;
        }

        case SLCAN_COMMAND_TYPE_SET_TIMESTAMPS:
            set_timestamps_enabled(command->timestamps_enabled);
            send_protocol_text(slcan_protocol_get_success_reply());
            return;

        /* Команда X — auto-poll compatibility stub для SavvyCAN.
           Bridge работает в непрерывном receive-loop, отдельная механика
           auto-poll не нужна. Принимаем X0/X1, отвечаем success. */
        case SLCAN_COMMAND_TYPE_SET_AUTO_POLL:
            send_protocol_text(slcan_protocol_get_success_reply());
            return;

        case SLCAN_COMMAND_TYPE_TRANSMIT_FRAME:
            if (can_bus_service_send_frame(&command->frame) == ESP_OK) {
                send_protocol_text(slcan_protocol_get_success_reply());
            } else {
                send_protocol_text(slcan_protocol_get_error_reply());
            }
            return;

        case SLCAN_COMMAND_TYPE_INVALID:
        default:
            send_protocol_text(slcan_protocol_get_error_reply());
            return;
    }
}

static void serial_command_task(void *task_parameter)
{
    (void)task_parameter;

    slcan_command_assembler_t assembler;
    slcan_command_assembler_reset(&assembler);

    char    command_buffer[SLCAN_BRIDGE_SLCAN_COMMAND_BUFFER_MAX_LENGTH] = {0};
    uint8_t receive_buffer[64] = {0};

    for (;;) {
        const int bytes_read = serial_transport_read_bytes(
            receive_buffer,
            sizeof(receive_buffer),
            pdMS_TO_TICKS(50));

        if (bytes_read <= 0) {
            continue;
        }

        for (int byte_index = 0; byte_index < bytes_read; ++byte_index) {
            bool command_ready      = false;
            bool command_overflowed = false;
            const bool accepted = slcan_command_assembler_consume_byte(
                &assembler,
                receive_buffer[byte_index],
                command_buffer,
                sizeof(command_buffer),
                &command_ready,
                &command_overflowed);

            if (!accepted || command_overflowed) {
                send_protocol_text(slcan_protocol_get_error_reply());
                continue;
            }

            if (!command_ready) {
                continue;
            }

            slcan_command_t command;
            if (!slcan_protocol_parse_command(command_buffer, &command)) {
                send_protocol_text(slcan_protocol_get_error_reply());
                continue;
            }

            handle_parsed_command(&command);
        }
    }
}

static void can_receive_task(void *task_parameter)
{
    (void)task_parameter;

    char encoded_frame[40] = {0};

    for (;;) {
        if (!can_bus_service_is_running()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        can_bus_frame_t frame;
        const esp_err_t receive_result = can_bus_service_receive_frame(
            &frame,
            pdMS_TO_TICKS(SLCAN_BRIDGE_CAN_RECEIVE_TIMEOUT_MILLISECONDS));
        if (receive_result != ESP_OK) {
            continue;
        }

        if (!serial_transport_is_connected()) {
            continue;
        }

        const size_t encoded_length = slcan_protocol_encode_frame(
            &frame,
            get_timestamps_enabled(),
            encoded_frame,
            sizeof(encoded_frame));

        send_protocol_buffer(encoded_frame, encoded_length);
    }
}

void bridge_application_start(void)
{
    runtime_state.settings_mutex = xSemaphoreCreateMutex();
    configASSERT(runtime_state.settings_mutex != NULL);

    serial_transport_initialize();
    can_bus_service_initialize();

    const BaseType_t command_task_result = xTaskCreate(
        serial_command_task,
        "slcan_command_task",
        SLCAN_BRIDGE_COMMAND_TASK_STACK_SIZE_BYTES,
        NULL,
        SLCAN_BRIDGE_TASKS_PRIORITY,
        NULL);
    configASSERT(command_task_result == pdPASS);

    const BaseType_t can_receive_task_result = xTaskCreate(
        can_receive_task,
        "slcan_can_rx_task",
        SLCAN_BRIDGE_CAN_RECEIVE_TASK_STACK_SIZE_BYTES,
        NULL,
        SLCAN_BRIDGE_TASKS_PRIORITY,
        NULL);
    configASSERT(can_receive_task_result == pdPASS);
}
