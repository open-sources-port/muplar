cmake_minimum_required(VERSION 4.3)

project(muplar
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(core)
add_subdirectory(cli)
add_subdirectory(compositor)
add_subdirectory(runtime)

# Only guest platforms
add_subdirectory(platform/windows)
add_subdirectory(platform/android)
