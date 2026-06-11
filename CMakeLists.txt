cmake_minimum_required(VERSION 4.3)

project(muplar
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_VERBOSE_MAKEFILE ON)

# =========================================
# elfuse: build via its own Makefile
# =========================================
include(ExternalProject)

set(ELFUSE_SRC_DIR    ${CMAKE_SOURCE_DIR}/third_party/elfuse)
set(ELFUSE_BUILD_DIR  ${ELFUSE_SRC_DIR}/build)
set(ELFUSE_STATIC_LIB ${CMAKE_BINARY_DIR}/libelfuse.a)

# --- Write a build script so we avoid ALL CMake shell-escaping issues ---
# cmake -P runs this directly; no shell quoting involved.
set(ELFUSE_BUILD_SCRIPT ${CMAKE_BINARY_DIR}/build_elfuse.cmake)
file(WRITE ${ELFUSE_BUILD_SCRIPT}
"# Build elfuse via its Makefile and produce libelfuse.a
set(ELFUSE_SRC_DIR  \"${ELFUSE_SRC_DIR}\")
set(ELFUSE_BUILD_DIR \"${ELFUSE_BUILD_DIR}\")
set(ELFUSE_STATIC_LIB \"${ELFUSE_STATIC_LIB}\")

# Step 1: run elfuse's Makefile with explicit target 'elfuse'
message(STATUS \"build_elfuse: running make elfuse ...\")
execute_process(
    COMMAND make elfuse CC=clang SIGN_IDENTITY=-
    WORKING_DIRECTORY \${ELFUSE_SRC_DIR}
    RESULT_VARIABLE RC
)
if(NOT RC EQUAL 0)
    message(FATAL_ERROR \"build_elfuse: make elfuse failed (exit \${RC})\")
endif()

# Step 2: collect .o files (exclude main.o — has a main() that conflicts)
file(GLOB_RECURSE ALL_OBJECTS \"\${ELFUSE_BUILD_DIR}/*.o\")
set(ELFUSE_OBJECTS \"\")
foreach(OBJ \${ALL_OBJECTS})
    if(NOT OBJ MATCHES \".*/main\\\\.o$\")
        list(APPEND ELFUSE_OBJECTS \"\${OBJ}\")
    endif()
endforeach()

if(NOT ELFUSE_OBJECTS)
    message(FATAL_ERROR
        \"build_elfuse: no .o files found under \${ELFUSE_BUILD_DIR}. \"
        \"Did make elfuse succeed?\")
endif()

# Step 3: create the static library
file(REMOVE \"\${ELFUSE_STATIC_LIB}\")
execute_process(
    COMMAND ar rcs \"\${ELFUSE_STATIC_LIB}\" \${ELFUSE_OBJECTS}
    RESULT_VARIABLE RC
)
if(NOT RC EQUAL 0)
    message(FATAL_ERROR \"build_elfuse: ar rcs failed (exit \${RC})\")
endif()
message(STATUS \"build_elfuse: created \${ELFUSE_STATIC_LIB}\")
"
)

# ExternalProject just invokes our cmake -P script.
# No shell, no escaping, no pipes.
ExternalProject_Add(elfuse_external
    SOURCE_DIR      ${ELFUSE_SRC_DIR}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND   ${CMAKE_COMMAND} -P ${ELFUSE_BUILD_SCRIPT}
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE 0
    BUILD_BYPRODUCTS
        ${ELFUSE_BUILD_DIR}/shim_blob.h
        ${ELFUSE_BUILD_DIR}/version.h
        ${ELFUSE_BUILD_DIR}/dispatch.h
        ${ELFUSE_STATIC_LIB}
)

# Custom target other targets can depend on
add_custom_target(elfuse_lib DEPENDS elfuse_external)

# Importable CMake target for target_link_libraries(... elfuse)
add_library(elfuse STATIC IMPORTED GLOBAL)
set_target_properties(elfuse PROPERTIES IMPORTED_LOCATION ${ELFUSE_STATIC_LIB})
add_dependencies(elfuse elfuse_lib)

# =========================================
# Wine: always built via autoconf
# =========================================
set(WINE_SRC_DIR    ${CMAKE_SOURCE_DIR}/third_party/wine)
set(WINE_BUILD_DIR  ${CMAKE_BINARY_DIR}/wine-build)
set(WINE_PREFIX_DIR ${CMAKE_BINARY_DIR}/wine-prefix)

set(WINE_BUILD_SHELL ${CMAKE_SOURCE_DIR}/tools/build-wine-x86_64.sh)

ExternalProject_Add(wine_external
    DOWNLOAD_COMMAND ""
    SOURCE_DIR      ${WINE_BUILD_DIR}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND   sh ${WINE_BUILD_SHELL}
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE 0
    USES_TERMINAL_BUILD YES
    BINARY_DIR      ${WINE_BUILD_DIR}
    BUILD_BYPRODUCTS
        ${WINE_PREFIX_DIR}/bin/wine
        ${WINE_PREFIX_DIR}/lib/libwine.dylib
)

add_custom_target(wine_lib DEPENDS wine_external)

# =========================================
# DXMT: install prebuilt binary into Wine
# =========================================
set(DXMT_INSTALL_SHELL ${CMAKE_SOURCE_DIR}/tools/build-dxmt-x86_64.sh)

if(APPLE)
    set(BREW_LLVM_PATH "/usr/local/opt/llvm@15")
else()
    set(BREW_LLVM_PATH "/usr/lib/llvm")
endif()

ExternalProject_Add(dxmt_external
    DOWNLOAD_COMMAND ""
    SOURCE_DIR      ${CMAKE_SOURCE_DIR}/third_party/dxmt
    CONFIGURE_COMMAND ""
    BUILD_COMMAND   ${CMAKE_COMMAND} -E env
                    DXMT_SRC_DIR=${CMAKE_SOURCE_DIR}/third_party/dxmt
                    DXMT_BUILD_DIR=${CMAKE_BINARY_DIR}/dxmt-build
                    WINE_INSTALL_PATH=${WINE_PREFIX_DIR}
                    LLVM_PATH=${BREW_LLVM_PATH}
                    sh ${DXMT_INSTALL_SHELL}
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE 0
    USES_TERMINAL_BUILD YES
    DEPENDS         wine_lib
    BUILD_BYPRODUCTS
        ${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/winemetal.so
        ${WINE_PREFIX_DIR}/lib/wine/x86_64-windows/winemetal.dll
        ${WINE_PREFIX_DIR}/lib/wine/x86_64-windows/d3d11.dll
        ${WINE_PREFIX_DIR}/lib/wine/x86_64-windows/dxgi.dll
)

add_custom_target(dxmt_lib DEPENDS dxmt_external)



# =========================================
# Linux Bootstrap: pre-downloaded Ubuntu Base rootfs
# Provides real distro coreutils for Linux instances
# (replaces BusyBox for Linux; Android still uses BusyBox)
# Run tools/setup-linux-bootstrap.sh once to download.
# =========================================
set(LINUX_BOOTSTRAP_DIR ${CMAKE_SOURCE_DIR}/third_party/linux-bootstrap)
set(LINUX_BOOTSTRAP_AARCH64 ${LINUX_BOOTSTRAP_DIR}/linux-bootstrap-aarch64.tar.gz)
set(LINUX_BOOTSTRAP_X86_64  ${LINUX_BOOTSTRAP_DIR}/linux-bootstrap-x86_64.tar.gz)

if(EXISTS "${LINUX_BOOTSTRAP_AARCH64}" OR EXISTS "${LINUX_BOOTSTRAP_X86_64}")
    message(STATUS "Linux bootstrap: found")
    set(MUPLAR_HAS_LINUX_BOOTSTRAP ON)
else()
    message(STATUS "Linux bootstrap: not found in source tree "
        "(will be downloaded dynamically at runtime if needed)")
    set(MUPLAR_HAS_LINUX_BOOTSTRAP OFF)
endif()

# =========================================
# Wawona: Wayland compositor
# =========================================
set(WAWONA_BUILD_SHELL ${CMAKE_SOURCE_DIR}/tools/build-muplar-wayland.sh)
set(WAWONA_BIN ${CMAKE_BINARY_DIR}/bin/wawona)

ExternalProject_Add(wawona_external
    DOWNLOAD_COMMAND ""
    SOURCE_DIR      ${CMAKE_SOURCE_DIR}/third_party/muplar-wayland
    CONFIGURE_COMMAND ""
    BUILD_COMMAND   sh ${WAWONA_BUILD_SHELL}
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE 0
    USES_TERMINAL_BUILD YES
    BINARY_DIR      ${CMAKE_BINARY_DIR}
    BUILD_BYPRODUCTS
        ${WAWONA_BIN}
)

add_custom_target(wawona_bin DEPENDS wawona_external)

# =========================================
# EGL/GLESv2 Guest Stubs
# =========================================
set(STUBS_BUILD_SHELL ${CMAKE_SOURCE_DIR}/tools/build-egl-gles-stubs.sh)
set(GUEST_ARM64_EGL_SO ${CMAKE_BINARY_DIR}/bin/aarch64/libEGL.so)
set(GUEST_ARM64_GLES_SO ${CMAKE_BINARY_DIR}/bin/aarch64/libGLESv2.so)
set(GUEST_X64_EGL_SO ${CMAKE_BINARY_DIR}/bin/x86_64/libEGL.so)
set(GUEST_X64_GLES_SO ${CMAKE_BINARY_DIR}/bin/x86_64/libGLESv2.so)

ExternalProject_Add(egl_gles_stubs_external
    DOWNLOAD_COMMAND ""
    SOURCE_DIR      ${CMAKE_SOURCE_DIR}/tools/guest-egl-gles-stubs
    CONFIGURE_COMMAND ""
    BUILD_COMMAND   sh ${STUBS_BUILD_SHELL}
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE 0
    USES_TERMINAL_BUILD YES
    BINARY_DIR      ${CMAKE_BINARY_DIR}
    BUILD_BYPRODUCTS
        ${GUEST_ARM64_EGL_SO}
        ${GUEST_ARM64_GLES_SO}
        ${GUEST_X64_EGL_SO}
        ${GUEST_X64_GLES_SO}
)

add_custom_target(egl_gles_stubs_bin DEPENDS egl_gles_stubs_external)

set(MUPLAR_WINE_PREFIX_DIR ${WINE_PREFIX_DIR})
set(MUPLAR_HAS_WINE ON)
message(STATUS "Muplar Windows Compatibility: enabled")
message(STATUS "Windows compatibility runtime: configured")

# =========================================
# Global include directories
# =========================================
include_directories(
    ${CMAKE_SOURCE_DIR}/third_party/elfuse/src
    ${ELFUSE_BUILD_DIR}   # shim_blob.h / version.h / dispatch.h
)

# =========================================
# Bundled JDK (Eclipse Temurin 21)
# Used for Java-only APK bootstrap via host JNI_CreateJavaVM.
# Run tools/setup-jdk.sh once to download.
# =========================================
if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(JDK_BIN_ARCH "macos-aarch64")
    else()
        set(JDK_BIN_ARCH "macos-x86_64")
    endif()

    set(JDK_BIN_DIR     ${CMAKE_SOURCE_DIR}/third_party/jdk-bin/${JDK_BIN_ARCH})
    set(JDK_LIBJVM      ${JDK_BIN_DIR}/lib/server/libjvm.dylib)
    set(JDK_LIBJLI      ${JDK_BIN_DIR}/lib/jli/libjli.dylib)
    set(JDK_INCLUDE_DIR ${JDK_BIN_DIR}/include)

    if(EXISTS "${JDK_LIBJVM}")
        message(STATUS "Bundled JDK: ${JDK_LIBJVM}")
        set(MUPLAR_HAS_BUNDLED_JDK ON)
    else()
        message(WARNING
            "Bundled JDK not found at ${JDK_BIN_DIR}\n"
            "  Run: tools/setup-jdk.sh\n"
            "  Java-only APK bootstrap will be disabled until then.")
        set(MUPLAR_HAS_BUNDLED_JDK OFF)
    endif()
endif()

# =========================================
# Subdirectories
# =========================================
# add_subdirectory(core)
add_subdirectory(cli)
# add_subdirectory(compositor)
add_subdirectory(platform)
add_subdirectory(apps)

find_program(DETECTED_OBJCOPY
    NAMES objcopy
    PATHS
        /opt/homebrew/opt/binutils/bin
        /usr/local/opt/binutils/bin
    NO_DEFAULT_PATH
    NO_CMAKE_SYSTEM_PATH
    NO_SYSTEM_ENVIRONMENT_PATH
)
