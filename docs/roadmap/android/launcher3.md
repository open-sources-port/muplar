# Launcher And Launcher3 Checklist

Stable area: installed-app discovery, Intent dispatch, Android UI/resources,
framework services, and the eventual AOSP Launcher3 compatibility target.

## Current Capability

- [x] Bundle and seed Muplar Simple Launcher per Android prefix.
- [x] Discover imported APKs in Instance Manager.
- [x] Read literal `android:label` values for display names.
- [x] Launch supported Java/native APKs from Instance Manager.
- [x] Provide a minimal launcher/settings compatibility UI.
- [x] Resolve resource-backed labels such as `@string/app_name`.

Muplar runs the official AOSP Launcher3 compatibility APK through startup,
model loading, All Apps interaction, and installed-application launch.
Advanced launcher features still require additional host UI and framework behavior.

## Next: Functional Simple Launcher

- [x] Build a per-prefix package registry from installed APK manifests.
- [x] Expose installed launchable activities through `PackageManager`.
- [x] Implement `getLaunchIntentForPackage()`.
- [x] Route explicit `Intent` components to a new Muplar app process.
- [x] Launch another installed APK from Muplar Simple Launcher.
- [x] Refresh the launcher when APKs are installed or removed.
- [x] Display application label and bitmap icon from APK resources.
- [x] Persist launcher/settings state per prefix.

Acceptance: Muplar Simple Launcher lists installed launchable APKs and can
start one by selecting it, without help from Instance Manager.

## Next: Android UI And Resources

- [x] Parse `resources.arsc` and resolve string, bitmap drawable, color, and dimension resources.
- [x] Inflate basic compiled XML layouts.
- [x] Implement core `View`, `ViewGroup`, layout, text, image, button, and list behavior.
- [x] Implement Activity lifecycle transitions across launched applications.
- [x] Connect Android window, input, focus, and configuration changes to the host surface.
- [x] Add baseline density, locale, theme, and orientation configuration support.

Acceptance: a small conventional Java APK using XML layouts and resources can
render and receive input without Muplar-specific UI APIs.

## Later: Framework Services

- [x] Start a persistent per-prefix `muplard` service daemon.
- [x] Route correlated Binder-style request/reply transactions over framed Unix sockets.
- [x] Connect the existing NDK Binder adapter to the `muplard` transaction router.
- [x] Add Java `Binder`, `Parcel`, and `ServiceManager` client adapters backed by `muplard`.
- [x] Support and verify file-descriptor passing with `SCM_RIGHTS`.
- [x] Implement `muplard`-backed ActivityManager process and top-activity tracking baseline.
- [x] Route PackageManager package-change broadcasts through `muplard`.
- [x] Move PackageManager metadata queries into `muplard` with file fallback.
- [x] Implement baseline `LauncherApps` queries and user-zero package callbacks.
- [x] Provide a minimal non-crashing `ShortcutManager` surface.
- [x] Provide a non-crashing `AppWidgetManager` baseline.
- [x] Provide baseline WindowManager/display and input-method discovery APIs.
- [x] Provide `system_server` service lifecycle through Muplar-owned daemon services.
- [x] Provide baseline SurfaceFlinger-compatible atomic surface state transactions.
- [x] Persist Settings provider values and system properties through `muplard`.
- [x] Enforce prefix-scoped package permissions and multi-user/profile isolation baseline.

Acceptance: framework applications can discover services and complete startup
without hard-coded app-specific substitutes.

## Later: AOSP Launcher3

Production Launcher3 must run through guest ART/app_process64 and Muplar's
native host-window/GPU path. The old host JVM/Swing launcher harness has been
removed from the production runtime and app bundle; Java APKs now enter through
guest ART only.

Required production inputs:

```sh
tools/import-android-art-sysroot.sh \
  --from ~/.muplar/sysroots/android-arm64/api-35 \
  --sysroot build/sysroot

tools/prepare-android-sysroot.sh \
  --android-root ~/.muplar/sysroots/android-arm64/api-35 \
  --sysroot build/sysroot \
  --strict-art

tools/check-android-art-sysroot.sh --sysroot build/sysroot
```

- [x] Pin the official Android 15 Launcher3 source revision and provenance workflow.
- [x] Add reproducible source fetch, AOSP APK import, and framework API inventory tooling.
- [x] Run manifest `Application.onCreate()` before Activity creation in Java APK processes.
- [x] Import and hash an official Android 15 ARM64 Launcher3 compatibility APK.
- [x] Build a pinned Launcher3 version as a compatibility fixture.
- [x] Reach Launcher3 process and Application initialization.
- [x] Reach the first Activity frame.
- [x] Populate the app drawer from PackageManager.
- [x] Launch an installed application from the app drawer.
- [x] Persist Launcher3 workspace and database state per prefix.
- [x] Display installed application labels and icons in Launcher3.
- [x] Propagate host configuration changes into Launcher3.
- [x] Populate and bind installable widgets.
- [x] Support workspace drag, drop, and item rearrangement.
- [x] Add Launcher3 startup and All Apps interaction smoke tests.
- [x] Add All Apps screenshot capture and nonblank visual validation.

Acceptance: Launcher3 displays an app drawer and launches an installed APK in
a clean Android prefix through guest ART/app_process64, not the host JVM/Swing
development harness. Advanced widgets and customization may follow.

Current runtime checkpoint: the official Android 15 ARM64 Launcher3 APK passes
multidex conversion, resource-table loading, process startup, and the complete
`LauncherApplication` and `QuickstepLauncher` lifecycle. Its model loader
completes workspace, all-apps, shortcut, widget, and icon-cache phases. The
prefix's two launchable APKs flow through Launcher3's `AllAppsStore` and
`AllAppsGridAdapter`, and the host RecyclerView bridge binds two genuine
`BubbleTextView` rows. A host pointer swipe reaches Launcher3's `AllApps` state,
and selecting a bound row follows Launcher3's `ItemClickHandler` path into
Muplar's intent dispatcher and launches the installed APK. Launcher databases
now use prefix-backed durable storage with schema versioning and CRUD support;
a headless two-open smoke verifies persistence. The host can also capture the
All Apps frame for nonblank visual validation. The next checkpoint is widget
binding and workspace drag/drop behavior.

## Current Launcher3 Smoke Status

Green checks:

- `cmake --build build --target mup populate_manager_bundle -j$(sysctl -n hw.ncpu)`
  builds the app bundle, ART bootstrap jar, Android ART shim, and packaged
  Launcher3 fixture.
- `platform/android-aarch64/compat/launcher3/smoke-launch.sh` reaches:
  - `ArtApkMain started`
  - `onStart/onResume completed successfully`
  - `makeVisible completed successfully`
  - `entering main looper`
- `platform/android-aarch64/compat/launcher3/visual-smoke.sh` produces
  `build/launcher3/verification/all-apps.png` and passes
  `tests/android/PngVisualSmoke.java`.
- Launcher3 `TaskView`, `GroupedTaskView`, and `DesktopTaskView` no longer
  fail during `ViewPool-init` because incompatible fixture/framework private
  attrs are bypassed in the bootstrap inflater.

Known limitations:

- The visual smoke PNG now uses the Android `Bitmap.compress()` path backed by
  ART-shim pixel storage. The deterministic PNG fallback remains only as a
  safety net.
- The Launcher3 view hierarchy is alive after `makeVisible`:
  `FrameLayout -> LauncherRootView -> DragLayer`, with Workspace, Hotseat,
  ScrimView, and Overview children laid out at real bounds.
- `View.draw(Canvas)` currently only reaches color/paint fills in the software
  snapshot (`color=2 paint=2 rect=0 text=0 bitmap=0` in the latest smoke).
  This means the next rendering gap is not native symbol resolution; it is the
  Java View/HWUI presentation path and Launcher3's empty home state.
- Instance Manager launches use `mup --apk --host-window`, but Java APK
  bootstrap enters `Looper.loop()` directly. That path does not return to the
  C++ host app loop, so `--host-window-ms` is not honored for Java APKs yet.
- The existing host window presents native/EGL/`ANativeWindow` buffers. Java
  Launcher3 View rendering is not safely connected to that host window yet.
  An experimental guest-side presenter is present but gated behind
  `MUPLAR_SOFTWARE_HOST_PRESENT=1` because direct guest-to-host native-window
  posting still crashes.
- The bundled Launcher3 fixture comes from the Android SDK default ARM64 system
  image, while the current framework sysroot is Samsung-flavored. Private
  framework resource IDs are not stable across those builds.
- The screenshot hook is for test automation only:
  `MUPLAR_LAUNCHER3_SCREENSHOT=/data/local/tmp/muplar/launcher3-visual-smoke.png`.

Required next steps:

1. Fix Java APK host-window control flow.
   - Do not let Java-only APK launches disappear forever inside
     `Looper.loop()` when `--host-window-ms` is set.
   - Let the C++ host app loop own pumping and timeout behavior for Java APKs,
     the same way it does for native-window apps.

2. Add a safe Java View-to-host-window presentation bridge.
   - Preferred short path: host-side frame handoff from the ART shim or
     bootstrap to `HostWindow::present_rgba`, without guest-side raw HVC calls.
   - Production path: model enough `ViewRootImpl`/`Surface`/HWUI behavior that
     Java framework rendering naturally posts to the existing host window.

3. Improve software Canvas content coverage.
   - Keep the bitmap backing storage and PNG compression path.
   - Add missing draw operations only when counters/logs show Launcher3 reaches
     them.
   - Once real Launcher3 pixels appear, remove the uniform-bitmap visual
     pattern from PNG compression.

4. Strengthen `visual-smoke.sh`.
   - Fail if the log says `fallback screenshot written` once real Bitmap
     compression is implemented.
   - Add checks for expected Launcher3 visual regions, not just nonblank pixels.

5. Align Launcher3 APK and framework resources.
   - Preferred production path: import a Launcher3 APK built from the same
     framework/sysroot image.
   - Keep the current SDK system-image fixture only as an early compatibility
     smoke fixture.

6. Verify through Instance Manager.
   - Direct `mup --apk` smoke passing is necessary but not enough.
   - Launch the packaged Launcher3 app through Instance Manager and confirm the
     same lifecycle/log behavior.

Useful commands:

```sh
cmake --build build --target mup populate_manager_bundle -j$(sysctl -n hw.ncpu)
platform/android-aarch64/compat/launcher3/smoke-launch.sh
platform/android-aarch64/compat/launcher3/visual-smoke.sh
rg -n "screenshot|fallback screenshot|UnsatisfiedLinkError|No implementation found" \
  "${TMPDIR:-/tmp}/muplar-launcher3-smoke.log"
```
