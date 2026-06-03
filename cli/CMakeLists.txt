add_executable(mup
    src/main.cpp
)

set(ANGLE_DYLIB_DIR "${CMAKE_SOURCE_DIR}/third_party/angle-bin")

set_target_properties(mup PROPERTIES
    BUILD_RPATH "@executable_path"
    INSTALL_RPATH "@executable_path;@executable_path/../Frameworks/angle"
    MACOSX_RPATH ON
)

target_link_libraries(mup
    PRIVATE
    muplar_runtime_memory
    muplar_runtime_elf
    muplar_runtime_jni
    muplar_runtime_android
    muplar_runtime_apk
    muplar_runtime_prefix
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
    ${CMAKE_SOURCE_DIR}/third_party/angle-include
    ${CMAKE_SOURCE_DIR}/platform/windows-x64
)

add_custom_command(TARGET mup POST_BUILD
    COMMAND codesign --entitlements "${CMAKE_SOURCE_DIR}/mup.entitlements" --force -s - "$<TARGET_FILE:mup>"
)

