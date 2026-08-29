idf_component_register(
    SRCS
        "camera/camera_mgr.cpp"
        "camera/ui_camera.cpp"
        "gallery/tjpgd.c"
        "gallery/ui_gallery.cpp"
        "recorder/audio_recorder.cpp"
        "recorder/ui_recorder.cpp"
        "chat/ai_storage.cpp"
        "chat/ai_client.cpp"
        "chat/ui_chat.cpp"
        "terminal/terminal_cmd.cpp"
        "terminal/ssh_client.cpp"
        "terminal/ui_terminal.cpp"
        "fileserver/http_file_server.cpp"
        "fileserver/ui_fileserver.cpp"
        "files/ui_files.cpp"
        "wifi/ui_wifi.cpp"
        "bluetooth/ui_bluetooth.cpp"
        "music/music_player.cpp"
        "music/ui_music.cpp"
        "calendar/ui_calendar.cpp"
    INCLUDE_DIRS
        "camera"
        "gallery"
        "recorder"
        "chat"
        "terminal"
        "fileserver"
        "files"
        "wifi"
        "bluetooth"
        "music"
        "calendar"
        "."
    REQUIRES os lvgl m5stack_tab5 libssh esp_video espressif__esp_cam_sensor
            esp_http_server esp_codec_dev esp_http_client json esp-tls
            mbedtls esp_audio_codec
)

target_link_libraries(
    ${COMPONENT_LIB}
    INTERFACE
        "-u sc202cs_detect"
        "-u sc2336_detect"
)
