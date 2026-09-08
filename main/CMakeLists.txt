idf_component_register(
  SRCS "app_main.cpp"
  INCLUDE_DIRS "."
  REQUIRES os rtc_rx8130 nvs_flash bt spiffs
)

execute_process(
  COMMAND ${CMAKE_SOURCE_DIR}/tools/ci/build_embedded_apps.sh
  RESULT_VARIABLE EMBEDDED_APPS_RESULT
)
if(NOT EMBEDDED_APPS_RESULT EQUAL 0)
  message(FATAL_ERROR "Falha ao compilar/empacotar aplicacoes embutidas: ${EMBEDDED_APPS_RESULT}")
endif()

spiffs_create_partition_image(apps ${CMAKE_SOURCE_DIR}/embedded_apps_pkg
                              FLASH_IN_PROJECT)
