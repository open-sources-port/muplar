# Launcher3 Status

Launcher3 is now the compatibility target for Android home-screen behavior.
This page should stay short: keep detailed implementation plans in focused
documents and use this page to answer, "Can Launcher3 run, and what blocks a
usable user experience?"

## Current State

- Launcher3 starts through guest ART/app_process64.
- `QuickstepLauncher` reaches `onCreate`, `onStart`, `onResume`,
  `makeVisible`, and the main looper.
- The Launcher3 view hierarchy is alive after `makeVisible`:
  `FrameLayout -> LauncherRootView -> DragLayer`, with Workspace, Hotseat,
  ScrimView, and Overview children laid out at real bounds.
- `visual-smoke.sh` can capture a PNG through the Android `Bitmap.compress()`
  path backed by ART-shim pixel storage.
- Instance Manager can list and launch the packaged Launcher3 APK into a
  persistent per-prefix Android session with a tabbed device window.
- Java View/HWUI frames present through `HostWindow`, the only host-side
  Android window handler. Launcher3 paints into the device window
  automatically.
- Back, Home, Recents, touch, and keyboard input reach the running Activity.
  This requires `MUPLAR_SERVICE_EXECUTABLE`/`MUPLAR_SERVICE_SOCKET` to be
  forwarded into the ART launch as `-Dmuplar.service.*` JVM properties;
  without them `FrameworkDeviceController` never subscribes to muplard's
  device-action/input streams and input silently does nothing.
- When Back finishes an app on the Java side, `FrameworkDeviceController`
  reports it to muplard (`tab-finished`/`query-tab-finished`) and the host
  polls after sending Back, auto-closing that tab in the device window.

## User-Visible Problem

The device window now behaves like a real Android device session: Launcher3
renders, app tabs open and close (each with its own close control), and Back/
Home/Recents/input all work. What is still missing is real task/back-stack
modeling, so multitasking does not yet match Android task semantics.

The product target is documented in:

- [Android Device Window](./device-window.md)

## Main Blockers

- Android task/back-stack state is host-simulated (simple tab list), not
  backed by real ActivityManager task tracking.
- The Java View frame path is still the software bitmap bridge
  (`MuplarFramePresenter` writing raw MHR frames); it should move to
  framework/HWUI-backed presentation.

## Verification Commands

```sh
cmake --build build --target mup populate_manager_bundle -j$(sysctl -n hw.ncpu)
platform/android-aarch64/compat/launcher3/smoke-launch.sh
platform/android-aarch64/compat/launcher3/visual-smoke.sh
rg -n "makeVisible|entering main looper|exit code|UnsatisfiedLinkError|No implementation found" \
  "${TMPDIR:-/tmp}/muplar-launcher3-smoke.log"
```

## Keep This Page Clean

Do not add long historical checklists here. If a task needs more than a few
bullets, create or update a focused roadmap document and link it from this
page.
