enable_language(OBJCXX)

add_executable(muplar_prefix_manager MACOSX_BUNDLE
    PrefixManagerApp.mm
)

set_target_properties(muplar_prefix_manager PROPERTIES
    OUTPUT_NAME "Muplar Instance Manager"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_link_libraries(muplar_prefix_manager
    PRIVATE
        muplar_runtime_prefix
        "-framework AppKit"
)

target_include_directories(muplar_prefix_manager
    PRIVATE
        ${CMAKE_SOURCE_DIR}/runtime/platform/commons/prefix
)

target_compile_options(muplar_prefix_manager PRIVATE
    -fobjc-arc
    -Wall
    -Wextra
)

add_custom_command(TARGET muplar_prefix_manager PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "${CMAKE_BINARY_DIR}/bin/Muplar Prefix Manager.app"
)
