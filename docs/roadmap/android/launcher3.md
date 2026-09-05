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
- Instance Manager can list, launch, and install (copy into `packages_dir`
  plus register in Instance Manager's own app list) the packaged Launcher3
  APK and additional APKs, all inside a persistent per-prefix Android
  session with a tabbed device window.
- Java View/HWUI frames present through `HostWindow`, the only host-side
  Android window handler. Launcher3 paints into the device window
  automatically.
- Device-action/input dispatch (Home, Recents, Settings, tab focus/close,
  touch, keyboard) goes through `MuplarSocketClient`, a small native
  `AF_UNIX` socket bridge in the ART shim — the guest connects to muplard's
  Unix socket directly, with no subprocess spawning. This replaced an
  earlier `ProcessBuilder`-spawned-`mup`-subprocess design that could not
  work: guest `execve()` of a host Mach-O binary does not work through
  elfuse (elfuse only loads Linux ELF binaries; confirmed via a kernel-panic
  investigation prompted by trying to force it to). Home/Recents/Settings/
  tab-switching are confirmed working end-to-end through this bridge.
- When Back finishes an app on the Java side, `FrameworkDeviceController`
  reports it to muplard (`tab-finished`/`query-tab-finished`) and the host
  polls after sending Back, auto-closing that tab in the device window.
- `android.view.MotionEvent`'s full native surface (all 44 methods this
  framework build declares, verified via `dexdump` against
  `framework-classes4.jar`) is implemented in the ART shim. Pointer events
  from the device window (`AndroidDeviceShell.mm` -> muplard `DeviceInput`
  opcode -> `FrameworkDeviceController.readInputs()`) reach
  `MotionEvent.obtain(...)` and construct a valid event object successfully.
- Touch input dispatch (2026-08-31): `MuplarContext.createWindowManager` and
  `ArtApkMain.attachWindow` were updated to resolve `WindowManagerImpl` through
  `baseContext` (preventing `Activity.getSystemService(WINDOW_SERVICE)` from
  returning `null` before `mWindowManager` is set). This resolved an NPE during
  `QuickstepLauncher.onCreate` (`UnfoldMoveFromCenterAnimator.<init>`), allowing
  the View hierarchy to fully initialize. Touch dispatch (`ACTION_DOWN` /
  `ACTION_UP`) via `muplard` `DeviceInput` now passes end-to-end through
  `dispatchTouchEvent` and `recycle()` without crashing (`test-touch-smoke.sh`
  passes cleanly).

## User-Visible Problem

The device window session/tab UI, Home/Recents/Settings dispatch, `LauncherApps`
installed-app query, touch input dispatch, secondary app launch, Back navigation,
and ART JIT execution all work now. The end-to-end product loop (Launcher3 → launch
app → Back to Launcher) is verified by `test-app-launch.sh`. The primary remaining
items to achieve a seamless "Android device" feel are real ActivityManager task/back-stack
management (P2) and moving from the 200ms-polled software-bitmap bridge to a proper
HWUI/Surface frame path (P4).

The product target is documented in:

- [Android Device Window](./device-window.md)

## Main Blockers

- ART JIT Stability (elfuse/HVF crash): **Resolved (2026-09-05)**.
  ART JIT is now enabled and running stably (`-Xusejit:true`). Root cause:
  elfuse pre-maps a 2 MiB RX window (`MMAP_RX_BASE=0x10000000` .. `MMAP_RX_INITIAL_END=0x10200000`).
  ART's default 64 MiB JIT code cache overflowed this window, invoking
  `guest_extend_page_tables` to map an RX 2 MiB L2 block descriptor adjacent
  to the RW heap, causing an HVF W^X violation and a native crash at the JIT
  entrypoint stub. Fixed by capping the JIT code cache to 1 MiB (`-Xjitmaxsize:1m`,
  `-Xjitinitialsize:512k`, `-Xjitthreshold:200`) in `art_bootstrap.cpp`.
  Touches, actions, and lifecycle methods now execute compiled code without crashing,
  eliminating the ~500% interpreter CPU spikes.
- Back button animation & Choreographer looper hangs: **Resolved (2026-09-05)**.
  `FrameworkDeviceController.performBack()` now unconditionally calls
  `onBackPressed` then `finishRecord`+`removeRecord`. No more `isFinishing()`
  polling or delayed fallback. `IActivityTaskManager.finishActivity` stubbed to
  return `Boolean.TRUE`. All `Handler` usage in device-action dispatch and frame
  scheduling uses `Handler.createAsync(looper)` to bypass the MessageQueue sync
  barrier (inserted by Choreographer/ViewRootImpl during vsync). End-to-end
  `test-app-launch.sh` passes: focus-tab launches secondary app, Back returns
  cleanly to Launcher3 (`activity resumed tab=launcher`).
- Touch input dispatch: **Resolved (2026-08-31)**. Touch input dispatch now
  executes cleanly through `dispatchTouchEvent` and `recycle()` without native
  crashes. Automated test coverage is added in `test-touch-smoke.sh`.
  Two bugs were fixed:
  1. Switched to the public 14-argument `MotionEvent.obtain(...)` overload,
     avoiding an interpreter argument-marshaling issue in `setSource()`.
  2. Fixed NPE in `QuickstepLauncher.onCreate` (`UnfoldMoveFromCenterAnimator`)
     by properly resolving `WindowManagerImpl` through `baseContext`.
- Android task/back-stack state: **P2 next step**. Currently host-simulated
  (a `LinkedHashMap` + stack in `FrameworkDeviceController`). True
  `ActivityManager` task tracking, intent resolution, and
  `startActivity`-from-app are needed for apps to launch each other correctly.
- The Java View frame path (`MuplarFramePresenter` → raw MHR bitmap) is a
  **P4 next step**. It should move to framework/HWUI-backed presentation via
  real `ViewRootImpl` / `Surface` semantics for smooth, choreographer-timed
  frame delivery and to remove the 200ms polling loop.

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
