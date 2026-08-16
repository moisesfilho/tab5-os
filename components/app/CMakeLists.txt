idf_component_register(
    SRCS
        "ui_shell.cpp"
        "ui_desktop.cpp"
        "ui_notas.cpp"
        "ui_wifi.cpp"
        "ui_files.cpp"
        "ui_bluetooth.cpp"
        "ui_keyboard.cpp"
        "ui_mouse.cpp"
        "ui_status.cpp"
        "ui_theme.cpp"
        "ui_bar.cpp"
        "imu_reader.cpp"
        "orientation.cpp"
        "wifi_mgr.cpp"
        "wifi_storage.cpp"
        "bt_mgr.cpp"
        "bt_storage.cpp"
        "display_storage.cpp"
        "file_assoc.cpp"
        "fonts/lv_font_montserrat_14_latin1.c"
        "fonts/lv_font_montserrat_28_latin1.c"
    INCLUDE_DIRS "." "include"
    REQUIRES lvgl m5stack_tab5 sensor_hub nvs_flash esp_wifi esp_netif esp_event
            fatfs sdmmc bt
)
