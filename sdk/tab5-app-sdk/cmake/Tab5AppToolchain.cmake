# Tab5AppToolchain.cmake
# Toolchain CMake para compilacao WebAssembly voltada ao Tab5 OS

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

if(DEFINED ENV{WASI_SDK_PATH})
  set(WASI_SDK_PREFIX "$ENV{WASI_SDK_PATH}")
elseif(EXISTS "/opt/wasi-sdk")
  set(WASI_SDK_PREFIX "/opt/wasi-sdk")
elseif(EXISTS "$ENV{HOME}/.tab5/wasi-sdk")
  set(WASI_SDK_PREFIX "$ENV{HOME}/.tab5/wasi-sdk")
endif()

if(DEFINED WASI_SDK_PREFIX)
  set(CMAKE_C_COMPILER "${WASI_SDK_PREFIX}/bin/clang")
  set(CMAKE_CXX_COMPILER "${WASI_SDK_PREFIX}/bin/clang++")
  set(CMAKE_AR "${WASI_SDK_PREFIX}/bin/llvm-ar")
  set(CMAKE_RANLIB "${WASI_SDK_PREFIX}/bin/llvm-ranlib")
else()
  find_program(CMAKE_C_COMPILER clang clang-21 clang-18 clang-17)
  find_program(CMAKE_CXX_COMPILER clang++ clang++-21 clang++-18 clang++-17)
endif()

set(CMAKE_C_FLAGS_INIT
    "--target=wasm32-wasi -O2 -fno-exceptions -Wall -Wextra")
set(CMAKE_CXX_FLAGS_INIT
    "--target=wasm32-wasi -O2 -fno-exceptions -fno-rtti -Wall -Wextra")
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-Wl,--allow-undefined -Wl,--export-dynamic -Wl,--no-entry")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
