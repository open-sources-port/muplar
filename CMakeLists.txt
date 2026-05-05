cmake_minimum_required(VERSION 4.3)

project(muplar
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_VERBOSE_MAKEFILE ON)

# =========================================
# Global include directories
# =========================================
include_directories(
    ${CMAKE_SOURCE_DIR}/android/bionic/include
)

# =========================================
# Subdirectories
# =========================================
# add_subdirectory(core)
add_subdirectory(cli)
# add_subdirectory(compositor)
add_subdirectory(platform)
add_subdirectory(runtime)

# Test
add_subdirectory(tests/platform/android/bionic/smoke)
