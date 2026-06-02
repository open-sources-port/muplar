add_library(muplar_runtime_linux_aarch64 INTERFACE)

target_link_libraries(muplar_runtime_linux_aarch64
    INTERFACE
        muplar_runtime_linux_common
        muplar_runtime_memory
        muplar_runtime_prefix
)
