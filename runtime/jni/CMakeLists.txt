# runtime/elf/CMakeLists.txt

# ── Source files ──────────────────────────────────────────────────────────────
add_library(muplar_runtime_jni STATIC
    jni_bridge.cpp
    jni_env.cpp
    jni_onload.cpp
)

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(muplar_runtime_jni
    PUBLIC
        ${CMAKE_SOURCE_DIR}/runtime/elf
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/elfuse/src
        # shim_blob.h / version.h / dispatch.h land here after elfuse make
        ${CMAKE_SOURCE_DIR}/third_party/elfuse/build
)

# ── Link dependencies ─────────────────────────────────────────────────────────
target_link_libraries(muplar_runtime_jni
    PUBLIC
        muplar_runtime_memory
        muplar_runtime_elf
)

# ── Compiler flags ────────────────────────────────────────────────────────────
target_compile_options(muplar_runtime_jni PRIVATE
    -Wall
    -Wextra
    -Wno-language-extension-token
)
