#include "slcan_protocol.h"
#include "slcan_bridge_project_configuration.h"

#include <stdio.h>
#include <string.h>

static const char *success_reply = "\r";
static const char *error_reply   = "\a";

static bool convert_hex_character_to_value(char hex_character, uint8_t *value)
{
    if (value == NULL) {
        return false;
    }
    if (hex_character >= '0' && hex_character <= '9') {
        *value = (uint8_t)(hex_character - '0');
        return true;
    }
    if (hex_character >= 'A' && hex_character <= 'F') {
        *value = (uint8_t)(hex_character - 'A' + 10);
        return true;
    }
    if (hex_character >= 'a' && hex_character <= 'f') {
        *value = (uint8_t)(hex_character - 'a' + 10);
        return true;
    }
    return false;
}

static bool parse_hex_value(const char *text, size_t digit_count, uint32_t *value)
{
    if (text == NULL || value == NULL || digit_count == 0U) {
        return false;
    }
    uint32_t parsed_value = 0U;
    for (size_t index = 0U; index < digit_count; ++index) {
        uint8_t nibble_value = 0U;
        if (!convert_hex_character_to_value(text[index], &nibble_value)) {
            return false;
        }
        parsed_value = (parsed_value << 4U) | nibble_value;
    }
    *value = parsed_value;
    return true;
}

static bool parse_data_bytes(const char *text, uint8_t data_length_code, uint8_t *data)
{
    if (text == NULL || data == NULL) {
        return false;
    }
    for (uint8_t byte_index = 0U; byte_index < data_length_code; ++byte_index) {
        uint32_t byte_value = 0U;
        if (!parse_hex_value(text + ((size_t)byte_index * 2U), 2U, &byte_value)) {
            return false;
        }
        data[byte_index] = (uint8_t)byte_value;
    }
    return true;
}

static bool parse_frame_command(const char *command_text, slcan_command_t *parsed_command)
{
    const char frame_type = command_text[0];
    const bool is_extended_identifier = (frame_type == 'T' || frame_type == 'R');
    const bool is_remote_frame        = (frame_type == 'r' || frame_type == 'R');
    const size_t identifier_digit_count = is_extended_identifier ? 8U : 3U;
    const size_t command_length = strlen(command_text);
    const size_t minimum_length = 1U + identifier_digit_count + 1U;

    if (command_length < minimum_length) {
        return false;
    }

    memset(parsed_command, 0, sizeof(*parsed_command));
    parsed_command->type                          = SLCAN_COMMAND_TYPE_TRANSMIT_FRAME;
    parsed_command->frame.is_extended_identifier  = is_extended_identifier;
    parsed_command->frame.is_remote_frame         = is_remote_frame;

    if (!parse_hex_value(command_text + 1, identifier_digit_count,
                         &parsed_command->frame.identifier)) {
        return false;
    }
    if (!is_extended_identifier && parsed_command->frame.identifier > 0x7FFU) {
        return false;
    }
    if (is_extended_identifier && parsed_command->frame.identifier > 0x1FFFFFFFU) {
        return false;
    }

    uint32_t data_length_code = 0U;
    if (!parse_hex_value(command_text + 1 + identifier_digit_count, 1U, &data_length_code)) {
        return false;
    }
    if (data_length_code > 8U) {
        return false;
    }
    parsed_command->frame.data_length_code = (uint8_t)data_length_code;

    const char *payload_text   = command_text + 1 + identifier_digit_count + 1;
    const size_t payload_length = strlen(payload_text);

    if (is_remote_frame) {
        return payload_length == 0U;
    }
    if (payload_length != (size_t)parsed_command->frame.data_length_code * 2U) {
        return false;
    }
    return parse_data_bytes(payload_text, parsed_command->frame.data_length_code,
                            parsed_command->frame.data);
}

static size_t append_hex_value(char *buffer, size_t buffer_length, size_t write_offset,
                                uint32_t value, size_t digit_count)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    if (buffer == NULL || buffer_length == 0U) {
        return write_offset;
    }
    for (size_t digit_index = 0U; digit_index < digit_count; ++digit_index) {
        const size_t shift = (digit_count - 1U - digit_index) * 4U;
        if (write_offset + 1U >= buffer_length) {
            return write_offset;
        }
        buffer[write_offset++] = hex_digits[(value >> shift) & 0x0FU];
    }
    return write_offset;
}

bool slcan_protocol_parse_command(const char *command_text, slcan_command_t *parsed_command)
{
    if (command_text == NULL || parsed_command == NULL || command_text[0] == '\0') {
        return false;
    }

    memset(parsed_command, 0, sizeof(*parsed_command));

    switch (command_text[0]) {
        case 'O':
            if (strcmp(command_text, "O") != 0) { return false; }
            parsed_command->type = SLCAN_COMMAND_TYPE_OPEN_NORMAL;
            return true;

        case 'L':
            if (strcmp(command_text, "L") != 0) { return false; }
            parsed_command->type = SLCAN_COMMAND_TYPE_OPEN_LISTEN_ONLY;
            return true;

        case 'C':
            if (strcmp(command_text, "C") != 0) { return false; }
            parsed_command->type = SLCAN_COMMAND_TYPE_CLOSE;
            return true;

        case 'S':
            if (strlen(command_text) != 2U) { return false; }
            parsed_command->type       = SLCAN_COMMAND_TYPE_SET_BITRATE;
            parsed_command->speed_code = command_text[1];
            return true;

        case 'F':
            if (strcmp(command_text, "F") != 0) { return false; }
            parsed_command->type = SLCAN_COMMAND_TYPE_READ_STATUS_FLAGS;
            return true;

        case 'V':
            if (strcmp(command_text, "V") != 0) { return false; }
            parsed_command->type = SLCAN_COMMAND_TYPE_GET_FIRMWARE_VERSION;
            return true;

        case 'N':
            if (strcmp(command_text, "N") != 0) { return false; }
            parsed_command->type = SLCAN_COMMAND_TYPE_GET_SERIAL_NUMBER;
            return true;

        case 'Z':
            if (strlen(command_text) != 2U ||
                (command_text[1] != '0' && command_text[1] != '1')) {
                return false;
            }
            parsed_command->type               = SLCAN_COMMAND_TYPE_SET_TIMESTAMPS;
            parsed_command->timestamps_enabled = (command_text[1] == '1');
            return true;

        /* Команда X — auto-poll (LAWICEL CAN232 manual).
           X0 = off, X1 = on. Любое другое значение отклоняем. */
        case 'X':
            if (strlen(command_text) != 2U ||
                (command_text[1] != '0' && command_text[1] != '1')) {
                return false;
            }
            parsed_command->type = SLCAN_COMMAND_TYPE_SET_AUTO_POLL;
            return true;

        case 't':
        case 'T':
        case 'r':
        case 'R':
            return parse_frame_command(command_text, parsed_command);

        default:
            return false;
    }
}

size_t slcan_protocol_encode_frame(const can_bus_frame_t *frame, bool include_timestamp,
                                    char *buffer, size_t buffer_length)
{
    if (frame == NULL || buffer == NULL || buffer_length < 2U) {
        return 0U;
    }

    /* Defensive clamp DLC в encode path. */
    const uint8_t safe_dlc = (frame->data_length_code > 8U) ? 8U : frame->data_length_code;

    size_t write_offset = 0U;
    buffer[write_offset++] = frame->is_extended_identifier
        ? (frame->is_remote_frame ? 'R' : 'T')
        : (frame->is_remote_frame ? 'r' : 't');

    write_offset = append_hex_value(buffer, buffer_length, write_offset,
                                    frame->identifier,
                                    frame->is_extended_identifier ? 8U : 3U);
    write_offset = append_hex_value(buffer, buffer_length, write_offset, safe_dlc, 1U);

    if (!frame->is_remote_frame) {
        for (uint8_t byte_index = 0U; byte_index < safe_dlc; ++byte_index) {
            write_offset = append_hex_value(buffer, buffer_length, write_offset,
                                            frame->data[byte_index], 2U);
        }
    }

    if (include_timestamp) {
        write_offset = append_hex_value(buffer, buffer_length, write_offset,
                                        frame->timestamp_milliseconds, 4U);
    }

    if (write_offset + 1U >= buffer_length) {
        return 0U;
    }

    buffer[write_offset++] = '\r';
    buffer[write_offset]   = '\0';
    return write_offset;
}

size_t slcan_protocol_encode_status_flags(uint8_t slcan_status_flags,
                                           char *buffer, size_t buffer_length)
{
    if (buffer == NULL || buffer_length < 5U) {
        return 0U;
    }
    buffer[0] = 'F';
    (void)append_hex_value(buffer, buffer_length, 1U, slcan_status_flags, 2U);
    buffer[3] = '\r';
    buffer[4] = '\0';
    return 4U;
}

size_t slcan_protocol_encode_firmware_version(char *buffer, size_t buffer_length)
{
    if (buffer == NULL || buffer_length < 7U) {
        return 0U;
    }
    const int written = snprintf(buffer, buffer_length, "V%.4s\r",
                                  SLCAN_BRIDGE_LAWICEL_FIRMWARE_VERSION);
    return (written > 0) ? (size_t)written : 0U;
}

size_t slcan_protocol_encode_serial_number(char *buffer, size_t buffer_length)
{
    if (buffer == NULL || buffer_length < 7U) {
        return 0U;
    }
    const int written = snprintf(buffer, buffer_length, "N%.4s\r",
                                  SLCAN_BRIDGE_LAWICEL_SERIAL_NUMBER);
    return (written > 0) ? (size_t)written : 0U;
}

const char *slcan_protocol_get_success_reply(void)
{
    return success_reply;
}

const char *slcan_protocol_get_error_reply(void)
{
    return error_reply;
}
