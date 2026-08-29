idf_component_register(
  SRCS "app_main.cpp"
  INCLUDE_DIRS "."
  REQUIRES os apps rtc_rx8130 nvs_flash bt spiffs
)

execute_process(COMMAND ${CMAKE_SOURCE_DIR}/tools/ci/build_embedded_apps.sh)

spiffs_create_partition_image(apps ${CMAKE_SOURCE_DIR}/embedded_apps_pkg
                              FLASH_IN_PROJECT)
