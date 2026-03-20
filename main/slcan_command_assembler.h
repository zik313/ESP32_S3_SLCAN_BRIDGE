#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t length;
    bool overflow_detected;
} slcan_command_assembler_t;

void slcan_command_assembler_reset(slcan_command_assembler_t *assembler);
bool slcan_command_assembler_consume_byte(
    slcan_command_assembler_t *assembler,
    uint8_t received_byte,
    char *command_buffer,
    size_t command_buffer_length,
    bool *command_ready,
    bool *command_overflowed);

#ifdef __cplusplus
}
#endif
