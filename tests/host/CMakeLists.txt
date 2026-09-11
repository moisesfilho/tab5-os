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

# "Boneco de Lata" (serial_bridge): o teste de contrato JSON->JSON compila
# sempre, mas só linka o módulo quando components/os/core/serial_bridge.cpp
# existir (fase vermelha do TDD: a suíte roda em SKIP documentado enquanto a
# implementação não chega). Ver tests/host/src/test_serial_bridge_dispatch.cpp.
set(SERIAL_BRIDGE_SRC "")
if(EXISTS ${REPO_ROOT}/components/os/core/serial_bridge.cpp)
  set(SERIAL_BRIDGE_SRC ${REPO_ROOT}/components/os/core/serial_bridge.cpp)
endif()

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
    src/test_wasm_unload_contract.cpp
    src/test_async_dispatcher_contract.cpp
    src/test_manifest.cpp
    src/test_package_mgr.cpp
    src/test_storage_mgr.cpp
    src/test_sdk_integration.cpp
    src/test_serial_bridge_dispatch.cpp
    stubs/cJSON.c
    mocks/path_redirect.cpp
    mocks/link_wrappers.cpp
    mocks/nvs_mock.cpp
    mocks/bsp_mock.cpp
    mocks/serial_bridge_subsystem_mocks.cpp
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
    ${REPO_ROOT}/components/os/runtime/tab5_wasm_dispatcher.cpp
    ${REPO_ROOT}/components/os/runtime/tab5_manifest.cpp
    ${REPO_ROOT}/components/os/runtime/tab5_package_mgr.cpp
    ${REPO_ROOT}/components/os/core/ai_storage.cpp
    ${REPO_ROOT}/components/os/core/terminal_cmd.cpp
    ${SERIAL_BRIDGE_SRC}
)

target_include_directories(tab5_host_tests PRIVATE
    stubs
    mocks
    ${REPO_ROOT}/components/os/core
    ${REPO_ROOT}/components/os/runtime
    ${REPO_ROOT}/components/os/runtime/include
    ${REPO_ROOT}/components/os/shell
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

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME runtime_rollback_static
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/tests/test_runtime_rollback.py)
add_test(NAME runtime_entrypoint_cleanup_contracts
  COMMAND ${Python3_EXECUTABLE}
          ${REPO_ROOT}/tests/test_runtime_entrypoint_cleanup_contracts.py)
add_test(NAME sdk_export_contract
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/tests/test_sdk_export_contract.py)
add_test(NAME wasm_unload_callback_contract
  COMMAND ${Python3_EXECUTABLE}
          ${REPO_ROOT}/tests/test_wasm_unload_callback_contract.py)
add_test(NAME async_dispatcher_contract
  COMMAND ${Python3_EXECUTABLE}
          ${REPO_ROOT}/tests/test_async_dispatcher_contract.py)
add_test(NAME tab5_cli_contract
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/tests/test_tab5_cli_contract.py)
add_test(NAME serial_bridge_kconfig_contract
  COMMAND ${Python3_EXECUTABLE}
          ${REPO_ROOT}/tests/test_serial_bridge_kconfig_contract.py)
add_test(NAME serial_bridge_stack_contract
  COMMAND ${Python3_EXECUTABLE}
          ${REPO_ROOT}/tests/test_serial_bridge_stack_contract.py)
