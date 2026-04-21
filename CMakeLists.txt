cmake_minimum_required(VERSION 4.3)

project(muplar
    VERSION 0.1.0
    LANGUAGES CXX
)

add_subdirectory(core)
add_subdirectory(cli)
add_subdirectory(compositor)
add_subdirectory(runtime)

# Only guest platforms
add_subdirectory(platform/windows)
add_subdirectory(platform/android)
