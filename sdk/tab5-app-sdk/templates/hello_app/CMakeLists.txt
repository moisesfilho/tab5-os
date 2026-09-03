cmake_minimum_required(VERSION 3.16)
project(hello_app C)

set(CMAKE_C_STANDARD 11)

# Inclui configurações do Tab5 SDK
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/Tab5AppConfig.cmake)

tab5_add_application(hello_app
    SOURCES
        src/main.c
)
