#include "slcan_command_assembler.h"

#include <string.h>

void slcan_command_assembler_reset(slcan_command_assembler_t *assembler)
{
    if (assembler == NULL) {
        return;
    }

    assembler->length = 0U;
    assembler->overflow_detected = false;
}

bool slcan_command_assembler_consume_byte(
    slcan_command_assembler_t *assembler,
    uint8_t received_byte,
    char *command_buffer,
    size_t command_buffer_length,
    bool *command_ready,
    bool *command_overflowed)
{
    if (assembler == NULL || command_buffer == NULL || command_buffer_length < 2U ||
        command_ready == NULL || command_overflowed == NULL) {
        return false;
    }

    *command_ready = false;
    *command_overflowed = false;

    if (received_byte == '\n') {
        return true;
    }

    if (received_byte == '\r') {
        if (assembler->overflow_detected) {
            *command_ready = false;
            *command_overflowed = true;
            slcan_command_assembler_reset(assembler);
            command_buffer[0] = '\0';
            return true;
        }

        command_buffer[assembler->length] = '\0';
        *command_ready = assembler->length > 0U;
        *command_overflowed = false;
        assembler->length = 0U;
        return true;
    }

    if (assembler->overflow_detected) {
        return true;
    }

    if (assembler->length + 1U >= command_buffer_length) {
        assembler->overflow_detected = true;
        assembler->length = 0U;
        command_buffer[0] = '\0';
        return true;
    }

    command_buffer[assembler->length++] = (char)received_byte;
    command_buffer[assembler->length] = '\0';
    return true;
}
