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
- Instance Manager can list and launch the packaged Launcher3 APK.

## User-Visible Problem

The current experience is not user friendly. Launching Android can show an
empty or minimally useful window, and each app launch still behaves too much
like a separate runtime process instead of one Android device session.

The product target is now documented in:

- [Android Device Window](./device-window.md)

## Main Blockers

- Java View rendering is not safely connected to the host Android device
  window yet. The existing host window path is strongest for native/EGL or
  `ANativeWindow` buffers, while Launcher3 is Java View/HWUI UI.
- `HostWindow` should be the default and only host-side Android window
  handler. Java View/HWUI frames and native/EGL frames should both present
  through that path.
- Android app launch should be routed into a persistent per-prefix Android
  session instead of spawning an isolated host-window process for each APK.
- Back, Home, and task switching need first-class host controls.

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
