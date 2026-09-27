idf_component_register(SRCS "main.c" "network.c" "usb_bridge.c" "leds.c" "sockets.c"
    INCLUDE_DIRS "." ".."
    REQUIRES usb esp_wifi esp_netif nvs_flash esp_http_client esp-tls mbedtls esp_timer esp_driver_gpio esp_driver_rmt)
