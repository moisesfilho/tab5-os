idf_component_register(
    SRCS
        "core/app_registry.cpp"
        "core/file_assoc.cpp"
        "core/timezone_mgr.cpp"
        "core/display_storage.cpp"
        "core/wifi_mgr.cpp"
        "core/wifi_storage.cpp"
        "core/bt_mgr.cpp"
        "core/bt_storage.cpp"
        "core/imu_reader.cpp"
        "core/orientation.cpp"
        "shell/ui_shell.cpp"
        "shell/ui_desktop.cpp"
        "shell/ui_bar.cpp"
        "shell/ui_status.cpp"
        "shell/ui_screensaver.cpp"
        "shell/ui_keyboard.cpp"
        "shell/ui_mouse.cpp"
        "shell/ui_theme.cpp"
        "shell/ui_app_bar.cpp"
        "fonts/lv_font_montserrat_14_latin1.c"
        "fonts/lv_font_montserrat_28_latin1.c"
    INCLUDE_DIRS "core" "shell" "fonts" "."
    REQUIRES lvgl m5stack_tab5 sensor_hub nvs_flash esp_wifi esp_netif esp_event
            fatfs sdmmc bt apps
)
