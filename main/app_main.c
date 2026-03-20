#include "bridge_application.h"

#include "esp_log.h"

void app_main(void)
{
    /*
     * Keep the serial line clean for LAWICEL/SLCAN traffic after boot.
     * ROM and bootloader messages may still appear during reset.
     */
    esp_log_level_set("*", ESP_LOG_NONE);
    bridge_application_start();
}
