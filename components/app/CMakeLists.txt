idf_component_register(
    SRCS "ui_keyboard.cpp" "ui_status.cpp" "ui_theme.cpp" "ui_bar.cpp" "imu_reader.cpp" "orientation.cpp" "fonts/lv_font_montserrat_14_latin1.c"
    INCLUDE_DIRS "." "include"
    REQUIRES lvgl m5stack_tab5 sensor_hub nvs_flash
)
