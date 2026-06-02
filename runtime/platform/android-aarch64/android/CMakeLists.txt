# runtime/platform/android-aarch64/android/CMakeLists.txt

# ── Source files ──────────────────────────────────────────────────────────────
set(MUPLAR_ANDROID_SOURCES
    android_runtime.cpp
)

if(APPLE)
    enable_language(OBJCXX)
    list(APPEND MUPLAR_ANDROID_SOURCES host_window_mac.mm)
else()
    list(APPEND MUPLAR_ANDROID_SOURCES host_window_stub.cpp)
endif()

add_library(muplar_runtime_android STATIC
    ${MUPLAR_ANDROID_SOURCES}
)

# ── ANGLE binaries (OpenGL ES → Metal on macOS) ───────────────────────────────
set(ANGLE_LIB_DIR ${CMAKE_SOURCE_DIR}/third_party/angle-bin)

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(muplar_runtime_android
    PUBLIC
        ${CMAKE_SOURCE_DIR}/runtime/platform/android-aarch64/android
        ${CMAKE_SOURCE_DIR}/runtime/platform/android-aarch64/elf
        ${CMAKE_SOURCE_DIR}/runtime/platform/android-aarch64/jni
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/elfuse/src
        ${CMAKE_SOURCE_DIR}/third_party/elfuse/build
        ${CMAKE_SOURCE_DIR}/third_party/angle-include
)

# ── Link dependencies ─────────────────────────────────────────────────────────
target_link_libraries(muplar_runtime_android
    PUBLIC
        muplar_runtime_memory
        muplar_runtime_elf
        muplar_runtime_jni
    PRIVATE
        # ANGLE dylibs — loaded at runtime via the RPATH below.
        # We link them so the host process has libEGL/libGLESv2 symbols
        # available when our EGL handlers call into them directly.
        ${ANGLE_LIB_DIR}/libEGL.dylib
        ${ANGLE_LIB_DIR}/libGLESv2.dylib
)

if(APPLE)
    target_link_libraries(muplar_runtime_android PRIVATE "-framework AppKit")
    set_source_files_properties(host_window_mac.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc"
    )
endif()

# Tell the dynamic linker where to find the dylibs at runtime
set_target_properties(muplar_runtime_android PROPERTIES
    BUILD_RPATH   "${ANGLE_LIB_DIR}"
    INSTALL_RPATH "${ANGLE_LIB_DIR}"
)

# ── Compiler flags ────────────────────────────────────────────────────────────
target_compile_options(muplar_runtime_android PRIVATE
    -Wall
    -Wextra
    -Wno-language-extension-token
)
