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
a clean Android prefix. Advanced widgets and customization may follow.

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
