add_executable(mup
    src/main.cpp
)

set(ANGLE_DYLIB_DIR "${CMAKE_SOURCE_DIR}/third_party/angle-bin")

set_target_properties(mup PROPERTIES
    BUILD_RPATH "${ANGLE_DYLIB_DIR};@executable_path"
    INSTALL_RPATH "@executable_path;@executable_path/../Frameworks/angle;${ANGLE_DYLIB_DIR}"
    MACOSX_RPATH ON
)

target_link_libraries(mup
    PRIVATE
    muplar_runtime_memory
    muplar_runtime_elf
    muplar_runtime_jni
    muplar_runtime_android
    muplar_runtime_linux_aarch64
    muplar_runtime_linux_x64
    muplar_runtime_apk
    muplar_runtime_prefix
    muplar_runtime_supervisor
)

if(MUPLAR_HAS_WINE)
    target_link_libraries(mup PRIVATE muplar_runtime_wine)
    target_compile_definitions(mup PRIVATE MUPLAR_HAS_WINE=1)
endif()

target_include_directories(mup PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/platform/commons/memory
    ${CMAKE_SOURCE_DIR}/platform/commons/prefix
    ${CMAKE_SOURCE_DIR}/platform/android-aarch64/elf
    ${CMAKE_SOURCE_DIR}/platform/android-aarch64/jni
    ${CMAKE_SOURCE_DIR}/platform/android-aarch64/apk
    ${CMAKE_SOURCE_DIR}/platform/linux-aarch64
    ${CMAKE_SOURCE_DIR}/platform/linux-x64
    ${CMAKE_SOURCE_DIR}/third_party/angle-include
    ${CMAKE_SOURCE_DIR}/platform/windows-x64
)

add_custom_command(TARGET mup POST_BUILD
    COMMAND install_name_tool -change ./libEGL.dylib @rpath/libEGL.dylib "$<TARGET_FILE:mup>"
    COMMAND install_name_tool -change ./libGLESv2.dylib @rpath/libGLESv2.dylib "$<TARGET_FILE:mup>"
    COMMAND codesign --entitlements "${CMAKE_SOURCE_DIR}/mup.entitlements" --force -s - "$<TARGET_FILE:mup>"
)
