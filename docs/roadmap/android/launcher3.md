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

Muplar cannot run AOSP Launcher3 yet. Launcher3 depends on substantial Android
framework and system-service behavior that the current host-JVM compatibility
surface does not provide.

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
- [ ] Provide `system_server` service lifecycle or compatible Muplar-owned substitutes.
- [ ] Provide SurfaceFlinger-compatible composition semantics where required.
- [x] Persist Settings provider values and system properties through `muplard`.
- [ ] Enforce package permissions and multi-user/profile isolation.

Acceptance: framework applications can discover services and complete startup
without hard-coded app-specific substitutes.

## Later: AOSP Launcher3

- [ ] Build a pinned Launcher3 version as a compatibility fixture.
- [ ] Reach Launcher3 process and Application initialization.
- [ ] Reach the first Activity frame.
- [ ] Populate the app drawer from PackageManager.
- [ ] Launch an installed application from the app drawer.
- [ ] Support workspace persistence, icons, widgets, drag/drop, and configuration changes.
- [ ] Add startup, interaction, and visual regression tests.

Acceptance: Launcher3 displays an app drawer and launches an installed APK in
a clean Android prefix. Advanced widgets and customization may follow.
