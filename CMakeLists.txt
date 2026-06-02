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
# Global include directories
# =========================================
include_directories(
    ${CMAKE_SOURCE_DIR}/third_party/elfuse/src
    ${ELFUSE_BUILD_DIR}   # shim_blob.h / version.h / dispatch.h
)

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
