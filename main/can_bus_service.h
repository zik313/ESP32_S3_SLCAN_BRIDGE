#pragma once

#include "can_bus_types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

void can_bus_service_initialize(void);
esp_err_t can_bus_service_set_speed_from_slcan_code(char speed_code);
char can_bus_service_get_selected_speed_code(void);
esp_err_t can_bus_service_start(can_bus_operating_mode_t operating_mode);
esp_err_t can_bus_service_stop(void);
bool can_bus_service_is_running(void);
can_bus_operating_mode_t can_bus_service_get_operating_mode(void);
esp_err_t can_bus_service_send_frame(const can_bus_frame_t *frame);
esp_err_t can_bus_service_receive_frame(can_bus_frame_t *frame, TickType_t timeout_ticks);
can_bus_status_snapshot_t can_bus_service_get_status_snapshot(void);

#ifdef __cplusplus
}
#endif
