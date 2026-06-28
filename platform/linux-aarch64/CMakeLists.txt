add_library(muplar_runtime_linux_aarch64 STATIC
    linux_aarch64_runtime.cpp
)

target_include_directories(muplar_runtime_linux_aarch64
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/platform/commons/runtime
        ${CMAKE_SOURCE_DIR}/platform/commons/prefix
        ${CMAKE_SOURCE_DIR}/platform/android-aarch64/android
        ${CMAKE_SOURCE_DIR}/platform/android-aarch64/elf
)

target_link_libraries(muplar_runtime_linux_aarch64
    PUBLIC
        muplar_runtime_linux_common
        muplar_runtime_memory
        muplar_runtime_prefix
        muplar_runtime_platform
        muplar_runtime_supervisor
)
