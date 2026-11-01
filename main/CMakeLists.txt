idf_component_register(SRCS "main.c" "rolloffesp_hardware.c" "rolloffesp_wifi_web.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_wifi esp_event esp_netif esp_http_server nvs_flash
                                     esp_driver_gpio esp_timer mdns)
