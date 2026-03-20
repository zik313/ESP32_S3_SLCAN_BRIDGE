#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_bus_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SLCAN_COMMAND_TYPE_INVALID = 0,
    SLCAN_COMMAND_TYPE_OPEN_NORMAL,
    SLCAN_COMMAND_TYPE_OPEN_LISTEN_ONLY,
    SLCAN_COMMAND_TYPE_CLOSE,
    SLCAN_COMMAND_TYPE_SET_BITRATE,
    SLCAN_COMMAND_TYPE_READ_STATUS_FLAGS,
    SLCAN_COMMAND_TYPE_GET_FIRMWARE_VERSION,
    SLCAN_COMMAND_TYPE_GET_SERIAL_NUMBER,
    SLCAN_COMMAND_TYPE_SET_TIMESTAMPS,
    /* ПРАВКА 6: тип для команды X (auto-poll compatibility с SavvyCAN). */
    SLCAN_COMMAND_TYPE_SET_AUTO_POLL,
    SLCAN_COMMAND_TYPE_TRANSMIT_FRAME,
} slcan_command_type_t;

typedef struct {
    slcan_command_type_t type;
    char                 speed_code;
    bool                 timestamps_enabled;
    can_bus_frame_t      frame;
} slcan_command_t;

bool   slcan_protocol_parse_command(const char *command_text, slcan_command_t *parsed_command);
size_t slcan_protocol_encode_frame(const can_bus_frame_t *frame, bool include_timestamp,
                                    char *buffer, size_t buffer_length);
size_t slcan_protocol_encode_status_flags(uint8_t slcan_status_flags,
                                           char *buffer, size_t buffer_length);
size_t slcan_protocol_encode_firmware_version(char *buffer, size_t buffer_length);
size_t slcan_protocol_encode_serial_number(char *buffer, size_t buffer_length);
const char *slcan_protocol_get_success_reply(void);
const char *slcan_protocol_get_error_reply(void);

#ifdef __cplusplus
}
#endif
