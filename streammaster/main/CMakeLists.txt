set(board_leds "leds.c")
set(gamepad_source "")
if(CONFIG_BT_BLUEDROID_ENABLED)
    set(gamepad_source "gamepad.c")
endif()
if(STREAMMASTER_BOARD STREQUAL "generic")
    set(board_leds "leds_none.c")
endif()
idf_component_register(SRCS "main.c" "network.c" "usb_bridge.c" "${board_leds}" "sockets.c" ${gamepad_source}
    INCLUDE_DIRS "." ".."
    PRIV_REQUIRES esp_app_format
    REQUIRES usb bt esp_wifi esp_netif nvs_flash esp_http_client esp-tls mbedtls esp_timer esp_driver_gpio esp_driver_rmt)
if(STREAMMASTER_BOARD STREQUAL "generic")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SM_GENERIC_BOARD=1)
endif()
