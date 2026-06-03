add_library(muplar_runtime_windows_x64 INTERFACE)

target_compile_definitions(muplar_runtime_windows_x64
    INTERFACE
        MUPLAR_HAS_WINE=1
        MUPLAR_WINE_PREFIX=${MUPLAR_WINE_PREFIX_DIR}
)
target_link_libraries(muplar_runtime_windows_x64
    INTERFACE
        muplar_runtime_memory
        muplar_runtime_prefix
)
add_dependencies(muplar_runtime_windows_x64 wine_lib)

# ── Wine runtime library ─────────────────────────────────────────────────
add_library(muplar_runtime_wine STATIC
    wine_runtime.cpp
)

target_include_directories(muplar_runtime_wine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}              # wine_runtime.h
        ${CMAKE_SOURCE_DIR}/platform/commons/runtime  # platform_runtime.h
        ${CMAKE_SOURCE_DIR}/platform/commons/prefix   # prefix.h
)

target_compile_definitions(muplar_runtime_wine
    PRIVATE
        MUPLAR_WINE_PREFIX="${MUPLAR_WINE_PREFIX_DIR}"
)

target_link_libraries(muplar_runtime_wine
    PUBLIC
        muplar_runtime_prefix
)

add_dependencies(muplar_runtime_wine wine_lib dxmt_lib)
