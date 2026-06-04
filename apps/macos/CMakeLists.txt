enable_language(OBJCXX)

set(APP_BUNDLE_NAME "Muplar Instance Manager")
set(APP_BUNDLE "${CMAKE_BINARY_DIR}/bin/${APP_BUNDLE_NAME}.app")
set(APP_CONTENTS "${APP_BUNDLE}/Contents")
set(WINE_PREFIX_DIR "${CMAKE_BINARY_DIR}/wine-prefix")

add_executable(muplar_prefix_manager MACOSX_BUNDLE
    PrefixManagerApp.mm
)

set_target_properties(muplar_prefix_manager PROPERTIES
    OUTPUT_NAME "${APP_BUNDLE_NAME}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH "@executable_path;@executable_path/../Frameworks/angle"
    BUILD_WITH_INSTALL_RPATH TRUE
)

target_link_libraries(muplar_prefix_manager
    PRIVATE
        muplar_runtime_prefix
        "-framework AppKit"
        "-framework UniformTypeIdentifiers"
)

target_include_directories(muplar_prefix_manager
    PRIVATE
        ${CMAKE_SOURCE_DIR}/platform/commons/prefix
)

target_compile_options(muplar_prefix_manager PRIVATE
    -fobjc-arc
    -Wall
    -Wextra
)

# wine_lib and muplar_runtime_wine are always built; ensure they complete before manager compilation.
add_dependencies(muplar_prefix_manager wine_lib muplar_runtime_wine)

# ── Clean stale bundle name from older builds ──────────────────────────────────
add_custom_command(TARGET muplar_prefix_manager PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "${CMAKE_BINARY_DIR}/bin/Muplar Prefix Manager.app"
)

# ── Populate bundle structure after each build ────────────────────────────────
add_custom_target(populate_manager_bundle ALL
    # Create directory skeleton
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${APP_CONTENTS}/MacOS"
        "${APP_CONTENTS}/Frameworks"
        "${APP_CONTENTS}/Frameworks/angle"
        "${APP_CONTENTS}/Frameworks/wine/bin"
        "${APP_CONTENTS}/Frameworks/wine/lib"
        "${APP_CONTENTS}/Frameworks/wine/share/wine"

    # ── mup CLI next to the main executable ───────────────────────────────────
    COMMAND bash -c
        "if [ -f '${CMAKE_BINARY_DIR}/bin/mup' ]; then \
             cp -f '${CMAKE_BINARY_DIR}/bin/mup' '${APP_CONTENTS}/MacOS/mup'; \
             install_name_tool -change ./libEGL.dylib @executable_path/../Frameworks/angle/libEGL.dylib '${APP_CONTENTS}/MacOS/mup'; \
             install_name_tool -change ./libGLESv2.dylib @executable_path/../Frameworks/angle/libGLESv2.dylib '${APP_CONTENTS}/MacOS/mup'; \
             install_name_tool -change @rpath/libEGL.dylib @executable_path/../Frameworks/angle/libEGL.dylib '${APP_CONTENTS}/MacOS/mup'; \
             install_name_tool -change @rpath/libGLESv2.dylib @executable_path/../Frameworks/angle/libGLESv2.dylib '${APP_CONTENTS}/MacOS/mup'; \
             codesign --entitlements '${CMAKE_SOURCE_DIR}/mup.entitlements' --force -s - '${APP_CONTENTS}/MacOS/mup'; \
         fi"

    # ── BusyBox Static Binaries → Frameworks/ ────────────────────────────────
    COMMAND bash -c
        "if [ -f '${CMAKE_BINARY_DIR}/bin/busybox_aarch64' ]; then \
             cp -f '${CMAKE_BINARY_DIR}/bin/busybox_aarch64' '${APP_CONTENTS}/Frameworks/busybox_aarch64'; \
             codesign --force -s - '${APP_CONTENTS}/Frameworks/busybox_aarch64'; \
         fi"
    COMMAND bash -c
        "if [ -f '${CMAKE_BINARY_DIR}/bin/busybox_x86_64' ]; then \
             cp -f '${CMAKE_BINARY_DIR}/bin/busybox_x86_64' '${APP_CONTENTS}/Frameworks/busybox_x86_64'; \
             codesign --force -s - '${APP_CONTENTS}/Frameworks/busybox_x86_64'; \
         fi"

    # ── ANGLE (libEGL / libGLESv2) → Frameworks/ ──────────────────────────────
    COMMAND bash -c
        "for lib in libEGL.dylib libGLESv2.dylib; do \
             src='${CMAKE_SOURCE_DIR}/third_party/angle-bin/'\"$lib\"; \
             if [ -f \"$src\" ]; then \
                 cp -f \"$src\" '${APP_CONTENTS}/Frameworks/angle/'; \
             fi; \
         done"
    COMMAND install_name_tool
        -change ./libGLESv2.dylib
        @loader_path/libGLESv2.dylib
        "${APP_CONTENTS}/Frameworks/angle/libEGL.dylib"
    COMMAND codesign --force -s - "${APP_CONTENTS}/Frameworks/angle/libGLESv2.dylib"
    COMMAND codesign --force -s - "${APP_CONTENTS}/Frameworks/angle/libEGL.dylib"

    # ── Wine binaries → Frameworks/wine/bin/ ───────────────────────────────────
    COMMAND bash -c
        "if [ -d '${WINE_PREFIX_DIR}/bin' ]; then \
             cp -Rf '${WINE_PREFIX_DIR}/bin/.' '${APP_CONTENTS}/Frameworks/wine/bin/'; \
         else \
             echo '[bundle] wine-prefix not built yet, skipping wine copy'; \
         fi"

    # ── Wine libraries → Frameworks/wine/lib/wine/ ─────────────────────────────
    COMMAND bash -c
        "if [ -d '${WINE_PREFIX_DIR}/lib/wine' ]; then \
             mkdir -p '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix'; \
             rm -f '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.6.dylib' \
                   '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.dylib' \
                   '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             cp -Rf '${WINE_PREFIX_DIR}/lib/wine/.' \
                    '${APP_CONTENTS}/Frameworks/wine/lib/wine/'; \
         fi"
    # ── DXMT libraries → Frameworks/wine/lib/wine/ ─────────────────────────────
    COMMAND bash -c
        "if [ -d '${WINE_PREFIX_DIR}/x86_64-windows' ]; then \
             mkdir -p '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-windows'; \
             cp -Rf '${WINE_PREFIX_DIR}/x86_64-windows/.' \
                    '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-windows/'; \
         fi"
    COMMAND bash -c
        "if [ -d '${WINE_PREFIX_DIR}/x86_64-unix' ]; then \
             mkdir -p '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix'; \
             rm -f '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.6.dylib' \
                   '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.dylib' \
                   '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             cp -Rf '${WINE_PREFIX_DIR}/x86_64-unix/.' \
                    '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/'; \
         fi"
    # ── Wine share → Frameworks/wine/share/wine/ ─────────────────────────────
    COMMAND bash -c
        "if [ -d '${WINE_PREFIX_DIR}/share/wine' ]; then \
             cp -Rf '${WINE_PREFIX_DIR}/share/wine/.' \
                    '${APP_CONTENTS}/Frameworks/wine/share/wine/'; \
         fi"

    # ── Bundle FreeType (and libpng dependency) into the unix lib dir to bypass Rosetta DYLD_* stripping ───
    COMMAND bash -c
        "if [ -d '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix' ] && [ -f '/usr/local/opt/freetype/lib/libfreetype.6.dylib' ]; then \
             rm -f '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.6.dylib' '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.dylib' '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             cp -f '/usr/local/opt/freetype/lib/libfreetype.6.dylib' '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.6.dylib'; \
             cp -f '/usr/local/opt/libpng/lib/libpng16.16.dylib' '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             chmod 755 '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.6.dylib' '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             install_name_tool -change /usr/local/opt/libpng/lib/libpng16.16.dylib @loader_path/libpng16.16.dylib '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.6.dylib'; \
             install_name_tool -id @loader_path/libfreetype.6.dylib '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.6.dylib'; \
             install_name_tool -id @loader_path/libpng16.16.dylib '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             ln -sf libfreetype.6.dylib '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.dylib'; \
             codesign --force -s - '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.6.dylib' '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libpng16.16.dylib'; \
         fi"
    COMMAND bash -c
        "mkdir -p '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix'; \
         if [ -f '/usr/local/opt/freetype/lib/libfreetype.6.dylib' ]; then \
             rm -f '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.6.dylib' '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.dylib' '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             cp -f '/usr/local/opt/freetype/lib/libfreetype.6.dylib' '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.6.dylib'; \
             cp -f '/usr/local/opt/libpng/lib/libpng16.16.dylib' '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             chmod 755 '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.6.dylib' '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             install_name_tool -change /usr/local/opt/libpng/lib/libpng16.16.dylib @loader_path/libpng16.16.dylib '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.6.dylib'; \
             install_name_tool -id @loader_path/libfreetype.6.dylib '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.6.dylib'; \
             install_name_tool -id @loader_path/libpng16.16.dylib '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             ln -sf libfreetype.6.dylib '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.dylib'; \
             codesign --force -s - '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.6.dylib' '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libpng16.16.dylib'; \
         fi"

    COMMENT "Populating ${APP_BUNDLE_NAME}.app bundle"
    VERBATIM
)

add_dependencies(populate_manager_bundle muplar_prefix_manager wine_lib dxmt_lib mup busybox_bin)
