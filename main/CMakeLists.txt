idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES os apps rtc_rx8130 nvs_flash bt
)
