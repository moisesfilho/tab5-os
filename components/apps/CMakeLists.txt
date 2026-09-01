idf_component_register(
    SRCS
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
        "wifi/ui_wifi.cpp"
        "bluetooth/ui_bluetooth.cpp"
        "music/music_player.cpp"
        "music/ui_music.cpp"
    INCLUDE_DIRS
        "recorder"
        "chat"
        "terminal"
        "fileserver"
        "wifi"
        "bluetooth"
        "music"
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
