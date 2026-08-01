add_library(muplar_runtime_supervisor
    supervisor_service.cpp
)

target_include_directories(muplar_runtime_supervisor PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/platform/commons/prefix
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/elfuse/src
)

target_link_libraries(muplar_runtime_supervisor
    PUBLIC
        muplar_runtime_prefix
)

if(APPLE)
    set(WAWONA_DIR "${CMAKE_SOURCE_DIR}/third_party/muplar-wayland")
    set(WAWONA_SOURCES
        ${WAWONA_DIR}/src/platform/macos/WWNCompositorBridge.m
        ${WAWONA_DIR}/src/platform/macos/WWNSettings.m
        ${WAWONA_DIR}/src/platform/macos/WWNPlatformCallbacks.m
        ${WAWONA_DIR}/src/platform/macos/ui/Helpers/WWNImageLoader.m
        ${WAWONA_DIR}/src/platform/macos/ui/Machines/WWNMachineProfileStore.m
        ${WAWONA_DIR}/src/platform/macos/ui/Machines/WWNMachinesCoordinator.m
        ${WAWONA_DIR}/src/platform/macos/ui/Settings/WWNPreferences.m
        ${WAWONA_DIR}/src/platform/macos/ui/Settings/WWNPreferencesManager.m
        ${WAWONA_DIR}/src/platform/macos/ui/About/WWNAboutPanel.m
        ${WAWONA_DIR}/src/platform/macos/ui/Settings/WWNSettingsModel.m
        ${WAWONA_DIR}/src/platform/macos/ui/Settings/WWNWaypipeRunner.m
        ${WAWONA_DIR}/src/platform/macos/ui/Settings/WWNSSHClient.m
        ${WAWONA_DIR}/src/platform/macos/ui/Settings/WWNSettingsSplitViewController.m
        ${WAWONA_DIR}/src/platform/macos/ui/Settings/WWNSettingsSidebarViewController.m
        ${WAWONA_DIR}/src/platform/macos/WWNWindow.m
        ${WAWONA_DIR}/src/platform/macos/WWNPopupWindow.m
        ${WAWONA_DIR}/src/platform/macos/WWNSettings.c
    )

    foreach(src ${WAWONA_SOURCES})
        if(src MATCHES "\\.m$")
            set_source_files_properties(${src} PROPERTIES LANGUAGE OBJC)
        endif()
    endforeach()

    add_library(muplar_wawona STATIC
        ${WAWONA_SOURCES}
    )

    target_include_directories(muplar_wawona PUBLIC
        ${WAWONA_DIR}/src
        ${WAWONA_DIR}/src/util
        ${WAWONA_DIR}/src/platform/macos
        ${WAWONA_DIR}/src/platform/macos/ui
        ${WAWONA_DIR}/src/platform/macos/ui/Helpers
        ${WAWONA_DIR}/src/platform/macos/ui/Machines
        ${WAWONA_DIR}/src/platform/macos/ui/Settings
        /opt/homebrew/include
    )

    target_compile_definitions(muplar_wawona PUBLIC
        USE_RUST_CORE=1
        WAWONA_VERSION="0.0.1"
        WAWONA_WAYLAND_VERSION="unknown"
        WAWONA_XKBCOMMON_VERSION="unknown"
        WAWONA_LZ4_VERSION="unknown"
        WAWONA_ZSTD_VERSION="unknown"
        WAWONA_LIBFFI_VERSION="unknown"
        WAWONA_SSHPASS_VERSION="unknown"
        WAWONA_WAYPIPE_VERSION="unknown"
    )

    target_compile_options(muplar_wawona PRIVATE
        -fobjc-arc
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-unused-function
        -Wno-unused-variable
        -Wno-implicit-float-conversion
        -Wno-deprecated-declarations
        -Wno-format-nonliteral
        -Wno-format-pedantic
        -Wno-unguarded-availability-new
    )

    target_link_directories(muplar_wawona INTERFACE
        /opt/homebrew/lib
        /opt/homebrew/opt/openssl@3/lib
    )

    target_link_libraries(muplar_wawona
        PUBLIC
            "${WAWONA_DIR}/target/release/libmuplar_wayland.a"
            pixman-1
            xkbcommon
            ssl
            crypto
            z
            "-framework AppKit"
            "-framework UniformTypeIdentifiers"
            "-framework Cocoa"
            "-framework QuartzCore"
            "-framework CoreVideo"
            "-framework CoreMedia"
            "-framework CoreGraphics"
            "-framework ColorSync"
            "-framework Metal"
            "-framework MetalKit"
            "-framework IOSurface"
            "-framework VideoToolbox"
            "-framework AVFoundation"
            "-framework Network"
            "-framework Security"
            "-ObjC"
    )

    add_dependencies(muplar_wawona wawona_external)

    target_link_libraries(muplar_runtime_supervisor PUBLIC muplar_wawona)
endif()
