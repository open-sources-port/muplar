add_library(muplar_runtime_prefix STATIC
    prefix.cpp
)

target_include_directories(muplar_runtime_prefix
    PUBLIC
        ${CMAKE_SOURCE_DIR}/platform/commons/prefix
        ${CMAKE_SOURCE_DIR}/platform/linux-common
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/elfuse/src
)

target_compile_options(muplar_runtime_prefix PRIVATE
    -Wall
    -Wextra
)
