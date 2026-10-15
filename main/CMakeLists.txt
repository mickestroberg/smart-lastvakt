idf_component_register(
    SRCS
        "main.c"
        "app_config.c"
        "app_time.c"
        "network_setup.c"
        "phase_source.c"
        "tibber_client.c"
        "ui.c"
        "fonts/lv_font_lastvakt_12.c"
        "fonts/lv_font_lastvakt_16.c"
        "fonts/lv_font_lastvakt_20.c"
        "fonts/lv_font_lastvakt_24.c"
        "../components/waveshare_rgb_lcd_port.c"
    INCLUDE_DIRS
        "."
        "fonts"
        "../components"
    PRIV_REQUIRES
        esp_event
        esp_http_client
        esp_http_server
        esp_netif
        esp_wifi
        json
        mbedtls
        nvs_flash)

idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl COMPONENT_LIB)
elseif("lvgl__lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
endif()

if(lvgl_lib)
    target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
endif()
