add_executable(mup
    src/main.cpp
)

set(ANGLE_DYLIB_DIR "${CMAKE_SOURCE_DIR}/third_party/angle-bin")

set_target_properties(mup PROPERTIES
    BUILD_RPATH "@executable_path"
    INSTALL_RPATH "@executable_path"
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

target_include_directories(mup PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/platform/commons/memory
    ${CMAKE_SOURCE_DIR}/platform/commons/prefix
    ${CMAKE_SOURCE_DIR}/platform/android-aarch64/elf
    ${CMAKE_SOURCE_DIR}/platform/android-aarch64/jni
    ${CMAKE_SOURCE_DIR}/platform/android-aarch64/apk
    ${CMAKE_SOURCE_DIR}/third_party/angle-include
)

add_custom_command(TARGET mup POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ANGLE_DYLIB_DIR}/libEGL.dylib"
            "$<TARGET_FILE_DIR:mup>/libEGL.dylib"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ANGLE_DYLIB_DIR}/libGLESv2.dylib"
            "$<TARGET_FILE_DIR:mup>/libGLESv2.dylib"

        COMMAND install_name_tool
            -change ./libEGL.dylib
            @executable_path/libEGL.dylib
            "$<TARGET_FILE:mup>"

        COMMAND install_name_tool
            -change ./libGLESv2.dylib
            @executable_path/libGLESv2.dylib
            "$<TARGET_FILE:mup>"

        COMMAND install_name_tool
            -change ./libGLESv2.dylib
            @loader_path/libGLESv2.dylib
            "$<TARGET_FILE_DIR:mup>/libEGL.dylib"

        COMMAND codesign --entitlements "${CMAKE_SOURCE_DIR}/mup.entitlements" --force -s - "$<TARGET_FILE:mup>"
    )

