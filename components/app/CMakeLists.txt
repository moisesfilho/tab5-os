idf_component_register(
    SRCS "ui_keyboard.cpp" "ui_status.cpp" "imu_reader.cpp" "orientation.cpp"
    INCLUDE_DIRS "." "include"
    REQUIRES lvgl m5stack_tab5 sensor_hub
)
