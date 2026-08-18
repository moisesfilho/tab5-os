idf_component_register(
    SRCS
        "ui_shell.cpp"
        "ui_desktop.cpp"
        "ui_notas.cpp"
        "ui_wifi.cpp"
        "ui_files.cpp"
        "ui_bluetooth.cpp"
        "ui_terminal.cpp"
        "ui_keyboard.cpp"
        "ui_mouse.cpp"
        "ui_status.cpp"
        "ui_theme.cpp"
        "ui_bar.cpp"
        "ui_screensaver.cpp"
        "terminal_cmd.cpp"
        "ssh_client.cpp"
        "imu_reader.cpp"
        "orientation.cpp"
        "wifi_mgr.cpp"
        "wifi_storage.cpp"
        "bt_mgr.cpp"
        "bt_storage.cpp"
        "display_storage.cpp"
        "file_assoc.cpp"
        "camera_mgr.cpp"
        "ui_camera.cpp"
        "ui_gallery.cpp"
        "ui_fileserver.cpp"
        "tjpgd.c"
        "http_file_server.cpp"
        "fonts/lv_font_montserrat_14_latin1.c"
        "fonts/lv_font_montserrat_28_latin1.c"
    INCLUDE_DIRS "." "include"
    REQUIRES lvgl m5stack_tab5 sensor_hub nvs_flash esp_wifi esp_netif esp_event
            fatfs sdmmc bt libssh esp_video espressif__esp_cam_sensor
            esp_http_server
)

target_link_libraries(
    ${COMPONENT_LIB}
    INTERFACE
        "-u sc202cs_detect"
        "-u sc2336_detect"
)
