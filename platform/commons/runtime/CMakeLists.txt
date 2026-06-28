# platform/commons/runtime/CMakeLists.txt

set(ANGLE_LIB_DIR ${CMAKE_SOURCE_DIR}/third_party/angle-bin)

add_library(muplar_runtime_platform STATIC
    gpu_bridge.cpp
    gpu_bridge.h
)

if(APPLE)
    enable_language(OBJCXX)
    target_sources(muplar_runtime_platform PRIVATE host_window_mac.mm)
else()
    target_sources(muplar_runtime_platform PRIVATE host_window_stub.cpp)
endif()

target_include_directories(muplar_runtime_platform
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/elfuse/src
        ${CMAKE_SOURCE_DIR}/third_party/elfuse/build
        ${CMAKE_SOURCE_DIR}/third_party/angle-include
)

target_link_libraries(muplar_runtime_platform
    PUBLIC
        muplar_runtime_prefix
    PRIVATE
        ${ANGLE_LIB_DIR}/libEGL.dylib
        ${ANGLE_LIB_DIR}/libGLESv2.dylib
)

if(APPLE)
    target_link_libraries(muplar_runtime_platform PRIVATE "-framework AppKit")
    set_source_files_properties(host_window_mac.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc"
    )
endif()

# Tell the dynamic linker where to find the dylibs at runtime
set_target_properties(muplar_runtime_platform PROPERTIES
    BUILD_RPATH   "${ANGLE_LIB_DIR}"
    INSTALL_RPATH "${ANGLE_LIB_DIR}"
)
