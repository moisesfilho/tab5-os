idf_component_register(
    SRCS "rx8130.cpp" "rtc_rx8130.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c m5stack_tab5 freertos
)