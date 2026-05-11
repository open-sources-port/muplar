# runtime/elf/CMakeLists.txt

# ── Source files ──────────────────────────────────────────────────────────────
add_library(muplar_runtime_elf STATIC
    elf_loader.cpp
    elf_mapper.cpp
    execution_context.cpp
    guest_runner.cpp
)

# ── Must wait for elfuse to build first ───────────────────────────────────────
# elfuse_lib generates shim_blob.h, version.h, dispatch.h into
# third_party/elfuse/build/.  guest_runner.cpp includes shim_blob.h,
# so this dependency must be declared or the compile races and loses.
add_dependencies(muplar_runtime_elf elfuse_lib)

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(muplar_runtime_elf
    PUBLIC
        ${CMAKE_SOURCE_DIR}/runtime/elf
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/elfuse/src
        # shim_blob.h / version.h / dispatch.h land here after elfuse make
        ${CMAKE_SOURCE_DIR}/third_party/elfuse/build
)

# ── Link dependencies ─────────────────────────────────────────────────────────
target_link_libraries(muplar_runtime_elf
    PUBLIC
        muplar_runtime_memory
    PRIVATE
        elfuse                  # libelfuse.a (all elfuse .o except main.o)
        "-framework Hypervisor" # required by vcpu_run_loop / hv_* calls
)

# ── Compiler flags ────────────────────────────────────────────────────────────
target_compile_options(muplar_runtime_elf PRIVATE
    -Wall
    -Wextra
    -Wno-language-extension-token
)
