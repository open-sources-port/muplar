cmake_minimum_required(VERSION 3.20)

add_library(muplar_compositor
    src/compositor.cpp
    src/MuWindow.cpp
)

target_include_directories(muplar_compositor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        /opt/homebrew/include   # ✅ correct
)

target_link_directories(muplar_compositor
    PUBLIC
        /opt/homebrew/lib
)

target_link_libraries(muplar_compositor
    PUBLIC
        muplar_core
        SDL2
)
