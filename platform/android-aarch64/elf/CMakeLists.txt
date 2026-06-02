# platform/android-aarch64/elf/CMakeLists.txt

# ── Source files ──────────────────────────────────────────────────────────────
add_library(muplar_runtime_elf STATIC
    elf_loader.cpp
    elf_mapper.cpp
    execution_context.cpp
    guest_runner.cpp
)

# ── Must wait for elfuse to build first ───────────────────────────────────────
add_dependencies(muplar_runtime_elf elfuse_lib)

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(muplar_runtime_elf
    PUBLIC
        ${CMAKE_SOURCE_DIR}/platform/android-aarch64/elf
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/elfuse/src
        ${CMAKE_SOURCE_DIR}/third_party/elfuse/build
        ${CMAKE_SOURCE_DIR}/platform/commons/memory
        ${CMAKE_SOURCE_DIR}/platform/android-commons
        ${CMAKE_SOURCE_DIR}/platform/android-aarch64/jni
        ${CMAKE_SOURCE_DIR}/platform/android-aarch64/android
        ${CMAKE_SOURCE_DIR}/third_party/angle-include
)

# ── Link dependencies ─────────────────────────────────────────────────────────
target_link_libraries(muplar_runtime_elf
    PUBLIC
        muplar_runtime_memory
    PRIVATE
        muplar_runtime_jni
        muplar_runtime_android
        elfuse
        "-framework Hypervisor"
)

# ── Compiler flags ────────────────────────────────────────────────────────────
target_compile_options(muplar_runtime_elf PRIVATE
    -Wall
    -Wextra
    -Wno-language-extension-token
)
