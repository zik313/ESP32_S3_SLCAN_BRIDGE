idf_component_register(
    SRCS
        "app_main.c"
        "bridge_application.c"
        "can_bus_service.c"
        "serial_transport.c"
        "slcan_command_assembler.c"
        "slcan_protocol.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_usb_serial_jtag esp_timer
)
