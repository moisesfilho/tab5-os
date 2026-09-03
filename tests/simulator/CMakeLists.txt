# Simulador host do tab5_os: UI real + LVGL vendido (managed_components)
# em janela SDL2. Capturas BMP sao comparadas contra goldens para
# regressao visual — sem alterar codigo de producao.
cmake_minimum_required(VERSION 3.16)
project(tab5_os_sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(SDL2 REQUIRED)

# LVGL do proprio repo (mesma versao do firmware) com o lv_conf.h do sim.
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h
    CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
add_subdirectory(${REPO_ROOT}/managed_components/lvgl__lvgl
    ${CMAKE_CURRENT_BINARY_DIR}/lvgl)
target_link_libraries(lvgl PUBLIC SDL2::SDL2)

set(OS ${REPO_ROOT}/components/os)

add_executable(tab5_sim
    main.cpp
    scenarios/sim_scenarios.cpp
    shims/shim_bsp.cpp
    shims/mock_core.cpp
    shims/mock_apps.cpp
    shims/sim_capture.cpp
    shims/sim_time.cpp

    # Mocks reaproveitados dos testes host (NVS em memoria + /sdcard -> tmpdir)
    ${REPO_ROOT}/tests/host/mocks/nvs_mock.cpp
    ${REPO_ROOT}/tests/host/mocks/path_redirect.cpp
    ${REPO_ROOT}/tests/host/mocks/link_wrappers.cpp

    # os/core real (sem readers de hardware, que viram mocks)
    ${OS}/core/app_registry.cpp
    ${OS}/core/file_assoc.cpp
    ${OS}/core/timezone_mgr.cpp
    ${OS}/core/display_storage.cpp
    ${OS}/core/audio_storage.cpp
    ${OS}/core/wifi_storage.cpp
    ${OS}/core/bt_storage.cpp
    ${OS}/core/calendar_logic.cpp
    ${OS}/core/tjpgd.c
    ${OS}/core/ai_storage.cpp
    ${OS}/core/terminal_cmd.cpp

    # Fontes bitmap (identicas ao firmware)
    ${OS}/fonts/lv_font_montserrat_14_latin1.c
    ${OS}/fonts/lv_font_montserrat_18_latin1.c
    ${OS}/fonts/lv_font_montserrat_28_latin1.c
    ${OS}/fonts/lv_font_montserrat_56_latin1.c
    ${OS}/fonts/lv_font_jetbrains_mono_14_clock.c
    ${OS}/fonts/lv_font_jetbrains_mono_18_clock.c

    # Shell real
    ${OS}/shell/ui_app_bar.cpp
    ${OS}/shell/ui_bar.cpp
    ${OS}/shell/ui_desktop.cpp
    ${OS}/shell/ui_keyboard.cpp
    ${OS}/shell/ui_mouse.cpp
    ${OS}/shell/ui_screen_off.cpp
    ${OS}/shell/ui_screensaver.cpp
    ${OS}/shell/ui_shell.cpp
    ${OS}/shell/ui_status.cpp
    ${OS}/shell/ui_theme.cpp
    ${OS}/shell/ui_calendar_view.cpp
    ${OS}/shell/ui_files_view.cpp
    ${OS}/shell/ui_camera_view.cpp
    ${OS}/shell/ui_gallery_view.cpp
    ${OS}/shell/ui_installer.cpp
    ${OS}/shell/ui_storage_view.cpp
    ${OS}/shell/ui_bluetooth_view.cpp
    ${OS}/shell/ui_chat_view.cpp
    ${OS}/shell/ui_fileserver_view.cpp
    ${OS}/shell/ui_music_view.cpp
    ${OS}/shell/ui_recorder_view.cpp
    ${OS}/shell/ui_terminal_view.cpp
    ${OS}/shell/ui_wifi_view.cpp
    ${OS}/core/storage_mgr.cpp
    ${OS}/runtime/tab5_package_mgr.cpp
    ${OS}/runtime/tab5_manifest.cpp
    ${OS}/runtime/tab5_ui_host.cpp
    ${OS}/runtime/tab5_sys_host.cpp
    ${OS}/runtime/tab5_lifecycle_host.cpp
    ${OS}/runtime/tab5_host_abi.cpp
    ${OS}/runtime/tab5_storage_sandbox.cpp
    ${OS}/runtime/tab5_wasm_runtime.cpp
    shims/cJSON/cJSON.c
)

target_include_directories(tab5_sim PRIVATE
    shims
    shims/cJSON
    scenarios
    ${OS}/core
    ${OS}/shell
    ${OS}/runtime
    ${OS}/runtime/include
)

target_compile_options(tab5_sim PRIVATE -Wall -Wextra -Wno-unused-parameter)

# fopen/open/mkdir/opendir/stat -> /sdcard no tmpdir; time/localtime_r ->
# relogio congelavel no modo cenario.
target_link_libraries(tab5_sim PRIVATE lvgl
    -Wl,--wrap=fopen
    -Wl,--wrap=open
    -Wl,--wrap=mkdir
    -Wl,--wrap=opendir
    -Wl,--wrap=stat
    -Wl,--wrap=time
    -Wl,--wrap=localtime_r
)

# bt_storage usa atoi() sem incluir <stdlib.h> (mesma correcao dos testes host)
set_source_files_properties(${OS}/core/bt_storage.cpp
    PROPERTIES COMPILE_OPTIONS "-include;cstdlib")
