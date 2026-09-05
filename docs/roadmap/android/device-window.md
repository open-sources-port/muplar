# Android Device Window

Goal: Android should feel like one persistent device session, similar to
MuMuPlayer or other Android players. Starting an Android prefix should open
one device window with navigation controls and Launcher3 as Home. Apps should
open inside that session as tabs, not as separate macOS windows. `HostWindow`
is the default and only host-side Android window handler.

## Step 1: Build The Device Window Shell

Status: implemented in Instance Manager as `AndroidDeviceShell`.

Create one persistent host window per Android prefix.

Expected behavior:

- Opening an Android instance shows an "Android Device" window.
- The window has a toolbar with Back, Home, Recents/App switcher, Install APK,
  Settings, and close controls.
- The content area is reserved for the Android frame.
- The window can show a clear loading state while the runtime starts.
- The user never sees a blank unexplained window.

Implementation notes:

- Keep this in Instance Manager first.
- Reuse the selected Android prefix as the session identity.
- The shell can initially show placeholder content while rendering is still
  being connected.

## Step 2: Start Or Reuse One Android Session Per Prefix

Status: partially implemented. Instance Manager now owns one Android
`NSTask` per prefix, reuses the existing device window/session on repeated app
launches, launches the process off the main thread, and stops reporting the app
as running while the device is being closed. Full ActivityManager routing into
an already-running guest session remains part of Step 3 and Step 4.

Replace the "one `mup --apk --host-window` process per app click" model with a
session controller.

Expected behavior:

- Starting the Android prefix creates or reuses one runtime session.
- `muplard` belongs to that prefix session.
- Launcher3 launches automatically as Home.
- App launches are sent to the existing session instead of creating another
  host window.

Implementation notes:

- Add an `AndroidSessionController` concept in the macOS app/runtime boundary.
- Keep process ownership, logs, and cleanup attached to the prefix.
- The session should survive app switches until the Android device window is
  closed.

## Step 3: Route App Launches Into Tabs

Status: **implemented** (2026-09-05). `AndroidDeviceShell` owns a host-managed
tab strip with a persistent Launcher tab. App tab focus sends APK path, package,
and Activity metadata through `muplard`; `FrameworkDeviceController` loads the
APK, instantiates `Application` + `Activity`, drives the full lifecycle
(`onCreate`/`onStart`/`onResume`/`makeVisible`), and registers the Activity in
the managed tab map. Secondary APK launch (`muplar-ui-test.apk`) is verified
end-to-end including `ListView` layout inflation. Full Android task/back-stack
modeling remains host-simulated (simple tab list), not backed by real
ActivityManager task tracking.

Apps should appear as tabs inside the Android device window.

Expected behavior:

- The first tab is `Launcher`.
- Launching an APK creates or focuses an app tab.
- Closing an app tab returns to Launcher or the previous tab.
- The Android instance remains alive when one app tab closes.

Implementation notes:

- Start with host-managed tabs in Instance Manager.
- Later, map tabs to Android task/activity records managed by `muplard`.
- Use application labels and icons from APK metadata for tab titles.

## Step 4: Add Back, Home, And Recents Controls

Status: **implemented** (2026-09-05). All toolbar actions (Back, Home, Recents,
Settings, Install APK, Close Tab) flow through the `DeviceAction` protocol via
`muplard`. The Java bootstrap subscribes and applies them on the main looper via
`Handler.createAsync` (async handlers bypass the MessageQueue sync barrier
inserted by Choreographer/ViewRootImpl — critical for reliable action delivery
under `-Xint`). Back now unconditionally calls `onBackPressed` then
`finishRecord`+`removeRecord`, restoring focus to the previous tab (Launcher3).
`IActivityTaskManager.finishActivity` is stubbed to return `Boolean.TRUE` so
`Activity.finish()` IPC does not silently no-op. End-to-end test
(`test-app-launch.sh`) verifies: Launcher3 starts → focus-tab launches secondary
app → `onResume` confirmed → back restores Launcher3. True Android
task/back-stack semantics are still host-simulated.

The toolbar should provide basic device navigation before all rendering is
perfect.

Expected behavior:

- Back dispatches to the active activity or task.
- Home switches to Launcher3.
- Recents/App switcher shows active app tabs/tasks.
- Keyboard shortcuts can mirror the toolbar actions later.

Implementation notes:

- First implementation can call into bootstrap/runtime hooks.
- Production implementation should route through ActivityManager-style state in
  `muplard`.
- Keep commands prefix-scoped so multiple Android prefixes can run later.

## Step 5: Connect Rendering To The Device Window

Status: **implemented** for the software-rendered path (2026-09-05).
`HostWindow` streams rendered RGBA frames over a per-prefix Unix socket;
`AndroidDeviceShell` receives them in the embedded phone screen area. Java Views
render through `MuplarFramePresenter`: the active decor view is drawn to a
software `Bitmap`, serialized as an MHR frame in the prefix rootfs, then picked
up by `HostWindow`. All `Handler` usage in `MuplarFramePresenter`,
`MuplarVsyncScheduler`, and `FrameworkDeviceController` now uses
`Handler.createAsync` to avoid blocking on the MessageQueue sync barrier.
The frame loop guard prevents unnecessary CPU burn when
`MUPLAR_ANDROID_SOFTWARE_FRAME_PATH` is not set. Basic pointer/key forwarding
from the embedded screen is implemented including macOS→Android key-code
translation. Touch dispatch (`test-touch-smoke.sh`) and visual capture
(`visual-smoke.sh`, 1080×1920) both pass.

Known remaining issues in this step:
- Frame rate feels slow under `-Xint` (pure interpreter): the main thread is
  too backlogged to service 200ms frame-loop runnables at full rate. JIT would
  help but is currently disabled due to elfuse/HVF instability (see
  [Launcher3 Status](./launcher3.md)).
- The Java View frame path (`MuplarFramePresenter` → raw MHR bitmap) should
  eventually move to framework/HWUI-backed presentation via real
  `ViewRootImpl` / `Surface` semantics.

Make Java and native Android UI draw into the same device window content area.

Expected behavior:

- Launcher3 paints into the device window automatically.
- Java View/HWUI apps and native/EGL apps share the same host presentation
  surface.
- All Android UI frames present through `HostWindow`; there is no separate
  Java-specific window handler.
- The device viewport is stable and scaled into the phone frame.
- Input events are clipped to the content area and delivered to the active tab.

Implementation notes:

- Keep `HostWindow` as the only host-side Android window handler.
- Treat the Java software frame bridge as an internal `HostWindow` frame
  source, not a second Java-specific window.
- Next rendering phase: enough `ViewRootImpl`, `Surface`, and HWUI behavior
  that framework rendering naturally posts frames into `HostWindow`.

## Current Priority

Steps 1–5 are now functionally complete for the software-rendered path.
All smoke tests pass (`smoke-launch.sh`, `test-touch-smoke.sh`,
`test-app-launch.sh`, `visual-smoke.sh`). The core product loop —
Launcher3 as Home → launch secondary app → Back returns to Launcher —
works end-to-end.

Next priorities, in order:

### P1 — JIT Stability (elfuse/HVF crash)
The guest runs `-Xint` (pure interpreter) because enabling JIT triggers a
native crash via an elfuse/HVF `mmap`/`mprotect(PROT_EXEC)` bug in ART's
jit-code-cache allocation. Under `-Xint` everything works but is slow:
touches are sluggish, frame rate is poor, and sustained CPU can hit ~500%
during touch-backlog draining. Root-causing and fixing the JIT crash is the
highest-leverage single fix available. See [Launcher3 Status](./launcher3.md)
for the crash diagnosis (anonymous executable region, tombstoned-null-deref).

### P2 — Real Android Task / Back-Stack
The tab model is currently host-simulated (a `LinkedHashMap` + stack in
`FrameworkDeviceController`). True `ActivityManager` task tracking, intent
resolution, and `startActivity`-from-app should be wired up so that apps can
launch each other and the back stack behaves correctly across all cases.

### P3 — App Install UX
APKs can be copied into `packages_dir` and registered in Instance Manager,
but there is no in-session install flow (drag-and-drop, file picker, or
`adb install`-equivalent). An install sheet inside the device window that
triggers APK copy + re-scan is the next user-visible feature.

### P4 — HWUI / Framework-Backed Rendering
Replace the `MuplarFramePresenter` software-bitmap bridge with real
`ViewRootImpl` / `Surface` semantics so framework rendering naturally posts
frames into `HostWindow`. This removes the 200ms polling loop and enables
smooth, choreographer-timed frame delivery.
