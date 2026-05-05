# android/bionic/CMakeLists.txt

execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
    OUTPUT_VARIABLE CLANG_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE MACOS_SDK
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# =========================================
# Source files
# =========================================
set(BIONIC_SOURCES
    kernel/system_call.c
    kernel/mqueue.c
    kernel/mqueue_notify.c
)

# =========================================
# Create static library
# =========================================
add_library(bionic STATIC
    ${BIONIC_SOURCES}
)

# =========================================
# Optional shared library
# =========================================
# add_library(bionic_shared SHARED
#     ${BIONIC_SOURCES}
# )

# =========================================
# Include directories
# =========================================
target_include_directories(bionic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/linux
    ${CMAKE_CURRENT_SOURCE_DIR}/include/asm
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sys_compat
    ${CMAKE_CURRENT_SOURCE_DIR}/include/basic
    ${CMAKE_CURRENT_SOURCE_DIR}/include/bits
    ${CMAKE_CURRENT_SOURCE_DIR}/include/bits/fortify
)

# =========================================
# Compiler options
# =========================================
target_compile_options(bionic PRIVATE -v
    -Wall
    -Wextra
)

# target_include_directories(bionic PRIVATE
#     ${CMAKE_CURRENT_SOURCE_DIR}/include
#     ${CLANG_RESOURCE_DIR}/include
# )

# =========================================
# Platform specific
# =========================================
if(WIN32)
    target_compile_definitions(bionic PRIVATE
        PLATFORM_WINDOWS
    )
elseif(APPLE)
    target_compile_definitions(bionic PRIVATE
        PLATFORM_MACOS
    )
elseif(UNIX)
    target_compile_definitions(bionic PRIVATE
        PLATFORM_LINUX
        _FORTIFY_SOURCE=0
    )
endif()

# =========================================
# Force linker language
# =========================================
set_target_properties(bionic PROPERTIES
    LINKER_LANGUAGE C
)

# =========================================
# Subdirectories
# =========================================
# add_subdirectory(libc)
# add_subdirectory(libdl)
# add_subdirectory(libm)
# add_subdirectory(linker)
