#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t identifier;
    uint8_t data_length_code;
    uint8_t data[8];
    bool is_extended_identifier;
    bool is_remote_frame;
    uint16_t timestamp_milliseconds;
} can_bus_frame_t;

typedef enum {
    CAN_BUS_OPERATING_MODE_NORMAL = 0,
    CAN_BUS_OPERATING_MODE_LISTEN_ONLY = 1,
} can_bus_operating_mode_t;

typedef struct {
    bool driver_installed;
    bool bus_running;
    can_bus_operating_mode_t operating_mode;
    char selected_speed_code;
    uint8_t slcan_status_flags;
} can_bus_status_snapshot_t;

#ifdef __cplusplus
}
#endif
