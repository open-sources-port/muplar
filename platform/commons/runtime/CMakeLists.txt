add_library(muplar_runtime_platform INTERFACE)

target_include_directories(muplar_runtime_platform
    INTERFACE
        ${CMAKE_SOURCE_DIR}/platform/commons/runtime
)

target_link_libraries(muplar_runtime_platform
    INTERFACE
        muplar_runtime_prefix
)
