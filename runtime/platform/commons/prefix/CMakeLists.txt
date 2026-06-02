add_library(muplar_runtime_prefix STATIC
    prefix.cpp
)

target_include_directories(muplar_runtime_prefix
    PUBLIC
        ${CMAKE_SOURCE_DIR}/runtime/platform/commons/prefix
)

target_compile_options(muplar_runtime_prefix PRIVATE
    -Wall
    -Wextra
)
