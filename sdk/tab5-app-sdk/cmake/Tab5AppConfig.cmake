# Tab5AppConfig.cmake
# Helper CMake macros para empacotamento de aplicacoes Tab5 OS

get_filename_component(TAB5_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(TAB5_SDK_INCLUDE_DIRS "${TAB5_SDK_ROOT}/include")
set(TAB5_PACK_TOOL "${TAB5_SDK_ROOT}/tools/pack.py")

# Adiciona e configura o alvo de build de uma aplicacao Tab5
function(tab5_add_application target_name)
  cmake_parse_arguments(app "" "MANIFEST" "SOURCES;INCLUDES" ${ARGN})

  if(NOT app_manifest)
    set(app_manifest "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json")
  endif()

  add_executable(${target_name} ${app_sources})
  set_target_properties(${target_name} PROPERTIES
                        OUTPUT_NAME "app.wasm"
                        SUFFIX "")

  target_include_directories(${target_name} PRIVATE
                             ${TAB5_SDK_INCLUDE_DIRS}
                             ${app_includes})

  add_custom_command(
    TARGET ${target_name} POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${TAB5_PACK_TOOL} ${CMAKE_CURRENT_SOURCE_DIR}
            -o ${CMAKE_CURRENT_BINARY_DIR}/dist
    COMMENT "Empacotando ${target_name} em .tab5pkg...")
endfunction()
