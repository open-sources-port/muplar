enable_language(OBJCXX)
enable_language(OBJC)

set(APP_BUNDLE_NAME "Muplar Instance Manager")
set(APP_BUNDLE "${CMAKE_BINARY_DIR}/bin/${APP_BUNDLE_NAME}.app")
set(APP_CONTENTS "${APP_BUNDLE}/Contents")
set(WINE_PREFIX_DIR "${CMAKE_BINARY_DIR}/wine-prefix")
set(BUILTIN_ANDROID_SOURCE_DIR
    "${CMAKE_SOURCE_DIR}/platform/android-aarch64/builtin-apps")
set(BUILTIN_ANDROID_DIR "${CMAKE_BINARY_DIR}/builtin-android")
file(GLOB_RECURSE BUILTIN_ANDROID_SOURCES CONFIGURE_DEPENDS
    "${BUILTIN_ANDROID_SOURCE_DIR}/launcher/*")
file(GLOB_RECURSE ANDROID_BOOTSTRAP_JAVA CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/platform/android-aarch64/java-bootstrap/*.java")

add_custom_command(
    OUTPUT
        "${BUILTIN_ANDROID_DIR}/muplar-launcher.apk"
        "${BUILTIN_ANDROID_DIR}/muplar-launcher-classes.jar"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BUILTIN_ANDROID_DIR}"
    COMMAND ${CMAKE_COMMAND} -E env
        "JAVA_HOME=${JDK_BIN_DIR}" "JAVAC=${JDK_JAVAC}" "JAR=${JDK_JAR}"
        zsh "${CMAKE_SOURCE_DIR}/tools/build-art-bootstrap-jar.sh"
            --sysroot "${CMAKE_BINARY_DIR}/sysroot"
    COMMAND ${CMAKE_COMMAND} -E env
        "JAVA_HOME=${JDK_BIN_DIR}" "JAVAC=${JDK_JAVAC}" "JAR=${JDK_JAR}"
        zsh "${BUILTIN_ANDROID_SOURCE_DIR}/build-launcher-apk.sh"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/sysroot/data/local/tmp/javalauncher.apk"
        "${BUILTIN_ANDROID_DIR}/muplar-launcher.apk"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/sysroot/data/local/tmp/javalauncher-classes.jar"
        "${BUILTIN_ANDROID_DIR}/muplar-launcher-classes.jar"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/build-art-bootstrap-jar.sh"
        "${BUILTIN_ANDROID_SOURCE_DIR}/build-launcher-apk.sh"
        ${BUILTIN_ANDROID_SOURCES}
        ${ANDROID_BOOTSTRAP_JAVA}
    VERBATIM)

add_custom_target(muplar_android_builtin_apps DEPENDS
    "${BUILTIN_ANDROID_DIR}/muplar-launcher.apk"
    "${BUILTIN_ANDROID_DIR}/muplar-launcher-classes.jar")

set(WAWONA_DIR "${CMAKE_SOURCE_DIR}/third_party/muplar-wayland")

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
        muplar_runtime_apk
        muplar_runtime_prefix
        muplar_runtime_supervisor
)

target_include_directories(muplar_prefix_manager
    PRIVATE
        ${CMAKE_SOURCE_DIR}/platform/commons/prefix
        ${CMAKE_SOURCE_DIR}/platform/commons/supervisor
        ${CMAKE_SOURCE_DIR}/platform/android-aarch64/apk
)

target_compile_definitions(muplar_prefix_manager PRIVATE
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

target_compile_options(muplar_prefix_manager PRIVATE
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

# wine_lib and muplar_runtime_wine are always built; ensure they complete before manager compilation.
add_dependencies(muplar_prefix_manager wine_lib muplar_runtime_wine wawona_external)

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
        "${APP_CONTENTS}/Resources"
        "${APP_CONTENTS}/Resources/tools"
        "${APP_CONTENTS}/Resources/android"
        "${APP_CONTENTS}/Frameworks"
        "${APP_CONTENTS}/Frameworks/aarch64"
        "${APP_CONTENTS}/Frameworks/x86_64"
        "${APP_CONTENTS}/Frameworks/angle"
        "${APP_CONTENTS}/Frameworks/wine/bin"
        "${APP_CONTENTS}/Frameworks/wine/lib"
        "${APP_CONTENTS}/Frameworks/wine/share/wine"

    # ── Linux rootfs provisioner script → Resources/tools/ ─────────────────────
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/tools/provision-linux-rootfs.sh"
        "${APP_CONTENTS}/Resources/tools/provision-linux-rootfs.sh"
    COMMAND chmod 755 "${APP_CONTENTS}/Resources/tools/provision-linux-rootfs.sh"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BUILTIN_ANDROID_DIR}/muplar-launcher.apk"
        "${APP_CONTENTS}/Resources/android/muplar-launcher.apk"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BUILTIN_ANDROID_DIR}/muplar-launcher-classes.jar"
        "${APP_CONTENTS}/Resources/android/muplar-launcher-classes.jar"

    # ── Windows compatibility defaults → Resources/ ─────────────────────────
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/platform/windows-x64/muplar-fonts.reg"
        "${APP_CONTENTS}/Resources/muplar-fonts.reg"

    # ── mup CLI next to the main executable ───────────────────────────────────
    COMMAND bash -c
        "if [ -f '${CMAKE_BINARY_DIR}/bin/mup' ]; then \
             cp -f '${CMAKE_BINARY_DIR}/bin/mup' '${APP_CONTENTS}/MacOS/mup'; \
             codesign --entitlements '${CMAKE_SOURCE_DIR}/mup.entitlements' --force -s - '${APP_CONTENTS}/MacOS/mup'; \
         fi"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:muplard>" "${APP_CONTENTS}/MacOS/muplard"
    COMMAND chmod 755 "${APP_CONTENTS}/MacOS/muplard"

    # ── Linux Bootstrap Rootfs → Frameworks/ ─────────────────────────────────
    COMMAND bash -c
        "if [ -d '${LINUX_BOOTSTRAP_DIR}' ]; then \
             find '${LINUX_BOOTSTRAP_DIR}' -name 'linux-bootstrap-*.tar.gz' -exec cp -f {} '${APP_CONTENTS}/Frameworks/' \\;; \
         fi"

    # ── Wawona Dependencies → Frameworks/ (with dependency bundling) ──────────
    COMMAND bash -c
        "brew_prefix=''; \
         for prefix in /opt/homebrew /usr/local; do \
             if [ -d \"\$prefix\" ]; then \
                 brew_prefix=\"\$prefix\"; \
                 break; \
             fi; \
         done; \
         if [ -n \"\$brew_prefix\" ]; then \
             echo '[bundle] Copying Wawona Homebrew dependencies from' \"\$brew_prefix\"; \
             for lib in libpixman-1.0.dylib libxkbcommon.0.dylib libssl.3.dylib libcrypto.3.dylib; do \
                 lib_src=\"\$brew_prefix/lib/\$lib\"; \
                 if [ -f \"\$lib_src\" ]; then \
                     cp -f \"\$lib_src\" '${APP_CONTENTS}/Frameworks/'\"\$lib\"; \
                     chmod 755 '${APP_CONTENTS}/Frameworks/'\"\$lib\"; \
                     install_name_tool -id @loader_path/\"\$lib\" '${APP_CONTENTS}/Frameworks/'\"\$lib\"; \
                 fi; \
             done; \
             patch_deps() { \
                 local file=\"\$1\"; \
                 local prefix=\"\$2\"; \
                 if [ -f \"\$file\" ]; then \
                     otool -L \"\$file\" | tail -n +2 | awk '{print \$1}' | while read -r dep; do \
                         case \"\$dep\" in \
                             *libpixman-1*.dylib) \
                                 install_name_tool -change \"\$dep\" \"\${prefix}libpixman-1.0.dylib\" \"\$file\"; \
                                 ;; \
                             *libxkbcommon*.dylib) \
                                 install_name_tool -change \"\$dep\" \"\${prefix}libxkbcommon.0.dylib\" \"\$file\"; \
                                 ;; \
                             *libssl*.dylib) \
                                 install_name_tool -change \"\$dep\" \"\${prefix}libssl.3.dylib\" \"\$file\"; \
                                 ;; \
                             *libcrypto*.dylib) \
                                 install_name_tool -change \"\$dep\" \"\${prefix}libcrypto.3.dylib\" \"\$file\"; \
                                 ;; \
                         esac; \
                     done; \
                 fi; \
             }; \
             patch_deps '${APP_CONTENTS}/MacOS/${APP_BUNDLE_NAME}' '@loader_path/../Frameworks/'; \
             patch_deps '${APP_CONTENTS}/MacOS/mup' '@loader_path/../Frameworks/'; \
             patch_deps '${APP_CONTENTS}/Frameworks/libssl.3.dylib' '@loader_path/'; \
             patch_deps '${APP_CONTENTS}/Frameworks/libcrypto.3.dylib' '@loader_path/'; \
             patch_deps '${APP_CONTENTS}/Frameworks/libxkbcommon.0.dylib' '@loader_path/'; \
             patch_deps '${APP_CONTENTS}/Frameworks/libpixman-1.0.dylib' '@loader_path/'; \
             for lib in libpixman-1.0.dylib libxkbcommon.0.dylib libssl.3.dylib libcrypto.3.dylib; do \
                 if [ -f '${APP_CONTENTS}/Frameworks/'\"\$lib\" ]; then \
                     codesign --force -s - '${APP_CONTENTS}/Frameworks/'\"\$lib\" 2>/dev/null || true; \
                 fi; \
             done; \
         fi"

    # ── Guest EGL/GLESv2 Stubs → Frameworks/ ─────────────────────────────────
    COMMAND bash -c
        "if [ -f '${CMAKE_BINARY_DIR}/bin/aarch64/libEGL.so' ]; then \
             cp -f '${CMAKE_BINARY_DIR}/bin/aarch64/libEGL.so' '${APP_CONTENTS}/Frameworks/aarch64/libEGL.so'; \
         fi"
    COMMAND bash -c
        "if [ -f '${CMAKE_BINARY_DIR}/bin/aarch64/libGLESv2.so' ]; then \
             cp -f '${CMAKE_BINARY_DIR}/bin/aarch64/libGLESv2.so' '${APP_CONTENTS}/Frameworks/aarch64/libGLESv2.so'; \
         fi"
    COMMAND bash -c
        "if [ -f '${CMAKE_BINARY_DIR}/bin/x86_64/libEGL.so' ]; then \
             cp -f '${CMAKE_BINARY_DIR}/bin/x86_64/libEGL.so' '${APP_CONTENTS}/Frameworks/x86_64/libEGL.so'; \
         fi"
    COMMAND bash -c
        "if [ -f '${CMAKE_BINARY_DIR}/bin/x86_64/libGLESv2.so' ]; then \
             cp -f '${CMAKE_BINARY_DIR}/bin/x86_64/libGLESv2.so' '${APP_CONTENTS}/Frameworks/x86_64/libGLESv2.so'; \
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
             echo '[bundle] Windows compatibility runtime not built yet, skipping copy'; \
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
        "ft_lib=''; png_lib=''; \
         for prefix in /usr/local /opt/homebrew; do \
             if [ -f \"\$prefix/opt/freetype/lib/libfreetype.6.dylib\" ] && [ -f \"\$prefix/opt/libpng/lib/libpng16.16.dylib\" ]; then \
                 ft_lib=\"\$prefix/opt/freetype/lib/libfreetype.6.dylib\"; \
                 png_lib=\"\$prefix/opt/libpng/lib/libpng16.16.dylib\"; \
                 break; \
             fi; \
         done; \
         if [ -d '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix' ] && [ -n \"\$ft_lib\" ]; then \
             rm -f '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.6.dylib' '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.dylib' '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             cp -f \"\$ft_lib\" '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.6.dylib'; \
             cp -f \"\$png_lib\" '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             chmod 755 '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.6.dylib' '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             install_name_tool -change \"\$png_lib\" @loader_path/libpng16.16.dylib '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.6.dylib'; \
             install_name_tool -id @loader_path/libfreetype.6.dylib '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.6.dylib'; \
             install_name_tool -id @loader_path/libpng16.16.dylib '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             ln -sf libfreetype.6.dylib '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.dylib'; \
             codesign --force -s - '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libfreetype.6.dylib' '${WINE_PREFIX_DIR}/lib/wine/x86_64-unix/libpng16.16.dylib'; \
         fi"
    COMMAND bash -c
        "mkdir -p '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix'; \
         ft_lib=''; png_lib=''; \
         for prefix in /usr/local /opt/homebrew; do \
             if [ -f \"\$prefix/opt/freetype/lib/libfreetype.6.dylib\" ] && [ -f \"\$prefix/opt/libpng/lib/libpng16.16.dylib\" ]; then \
                 ft_lib=\"\$prefix/opt/freetype/lib/libfreetype.6.dylib\"; \
                 png_lib=\"\$prefix/opt/libpng/lib/libpng16.16.dylib\"; \
                 break; \
             fi; \
         done; \
         if [ -n \"\$ft_lib\" ]; then \
             rm -f '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.6.dylib' '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.dylib' '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             cp -f \"\$ft_lib\" '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.6.dylib'; \
             cp -f \"\$png_lib\" '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             chmod 755 '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.6.dylib' '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             install_name_tool -change \"\$png_lib\" @loader_path/libpng16.16.dylib '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.6.dylib'; \
             install_name_tool -id @loader_path/libfreetype.6.dylib '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.6.dylib'; \
             install_name_tool -id @loader_path/libpng16.16.dylib '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libpng16.16.dylib'; \
             ln -sf libfreetype.6.dylib '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.dylib'; \
             codesign --force -s - '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libfreetype.6.dylib' '${APP_CONTENTS}/Frameworks/wine/lib/wine/x86_64-unix/libpng16.16.dylib'; \
         fi"

    # Re-sign after bundle resources and nested binaries are copied. The deep
    # pass also touches the bundled mup CLI, so preserve its Hypervisor entitlement.
    COMMAND codesign --entitlements "${CMAKE_SOURCE_DIR}/mup.entitlements"
        --force --deep -s - "${APP_BUNDLE}"

    COMMENT "Populating ${APP_BUNDLE_NAME}.app bundle"
    VERBATIM
)

add_dependencies(populate_manager_bundle muplar_prefix_manager
    muplar_android_builtin_apps wine_lib dxmt_lib mup muplard wawona_bin
    egl_gles_stubs_bin)
