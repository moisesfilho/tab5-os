# Testes unitarios em host nativo (Fase 30 do PLANO.md).
# Rodam fora do ESP-IDF: stubs de headers, mocks de link e --wrap para
# redirecionar /sdcard para um tmpdir — sem alterar codigo de producao.
cmake_minimum_required(VERSION 3.16)
project(tab5_os_host_tests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(HOST_TESTS_COVERAGE "Instrumenta o binario de testes com --coverage" ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

find_package(Threads REQUIRED)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(tab5_host_tests
    src/test_infra.cpp
    src/test_orientation.cpp
    src/test_file_assoc.cpp
    src/test_app_registry.cpp
    src/test_terminal_cmd.cpp
    src/test_timezone_mgr.cpp
    src/test_wifi_storage.cpp
    src/test_bt_storage.cpp
    src/test_bt_report_map.cpp
    src/test_ai_storage.cpp
    src/test_display_storage.cpp
    src/test_calendar_logic.cpp
    src/test_storage_sandbox.cpp
    src/test_host_abi.cpp
    src/test_wasm_runtime.cpp
    src/test_manifest.cpp
    src/test_package_mgr.cpp
    src/test_storage_mgr.cpp
    src/test_sdk_integration.cpp
    stubs/cJSON.c
    mocks/path_redirect.cpp
    mocks/link_wrappers.cpp
    mocks/nvs_mock.cpp
    mocks/bsp_mock.cpp
    ${REPO_ROOT}/components/os/core/orientation.cpp
    ${REPO_ROOT}/components/os/core/file_assoc.cpp
    ${REPO_ROOT}/components/os/core/app_registry.cpp
    ${REPO_ROOT}/components/os/core/storage_mgr.cpp
    ${REPO_ROOT}/components/os/core/timezone_mgr.cpp
    ${REPO_ROOT}/components/os/core/wifi_storage.cpp
    ${REPO_ROOT}/components/os/core/bt_storage.cpp
    ${REPO_ROOT}/components/os/core/hid_report_map.cpp
    ${REPO_ROOT}/components/os/core/display_storage.cpp
    ${REPO_ROOT}/components/os/core/calendar_logic.cpp
    ${REPO_ROOT}/components/os/runtime/tab5_storage_sandbox.cpp
    ${REPO_ROOT}/components/os/runtime/tab5_ui_host.cpp
    ${REPO_ROOT}/components/os/runtime/tab5_sys_host.cpp
    ${REPO_ROOT}/components/os/runtime/tab5_lifecycle_host.cpp
    ${REPO_ROOT}/components/os/runtime/tab5_host_abi.cpp
    ${REPO_ROOT}/components/os/runtime/tab5_wasm_runtime.cpp
    ${REPO_ROOT}/components/os/runtime/tab5_manifest.cpp
    ${REPO_ROOT}/components/os/runtime/tab5_package_mgr.cpp
    ${REPO_ROOT}/components/apps/chat/ai_storage.cpp
    ${REPO_ROOT}/components/apps/terminal/terminal_cmd.cpp
)

target_include_directories(tab5_host_tests PRIVATE
    stubs
    mocks
    ${REPO_ROOT}/components/os/core
    ${REPO_ROOT}/components/os/runtime
    ${REPO_ROOT}/components/os/runtime/include
    ${REPO_ROOT}/components/apps/chat
    ${REPO_ROOT}/components/apps/terminal
)

target_compile_options(tab5_host_tests PRIVATE -Wall -Wextra)
target_link_libraries(tab5_host_tests
    PRIVATE GTest::gtest_main Threads::Threads)

# bt_storage.cpp usa atoi() sem incluir <stdlib.h> (no ESP-IDF o header
# chega transitivamente); em host forcamos a inclusao via compilador.
set_source_files_properties(${REPO_ROOT}/components/os/core/bt_storage.cpp
    PROPERTIES COMPILE_OPTIONS "-include;cstdlib")

if(HOST_TESTS_COVERAGE)
  target_compile_options(tab5_host_tests PRIVATE --coverage)
  target_link_options(tab5_host_tests PRIVATE --coverage)
endif()

target_link_options(tab5_host_tests PRIVATE
    -Wl,--wrap=fopen
    -Wl,--wrap=open
    -Wl,--wrap=mkdir
    -Wl,--wrap=opendir
    -Wl,--wrap=stat
)

enable_testing()
include(GoogleTest)
gtest_discover_tests(tab5_host_tests)
