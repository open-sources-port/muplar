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
- The Java View frame path (`MuplarFramePresenter` → raw MHR bitmap) should
  eventually move to framework/HWUI-backed presentation via real
  `ViewRootImpl` / `Surface` semantics (P4).

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

## Current Priority: P4 — HWUI / Framework-Backed Rendering

Steps 1–5 are functionally complete for the software-rendered path,
**P1 (ART JIT Stability)** is complete with ART JIT enabled and stable,
**P2 (Real Android Task / Back-Stack)** is complete with intra-app `startActivity`,
`finishActivity`, task stack management, and verified back navigation, and
**P3 (App Install UX)** is complete with in-session drag-and-drop / file picker installation and dynamic Launcher3 app drawer refresh.
All smoke and end-to-end tests pass (`smoke-launch.sh`, `test-touch-smoke.sh`,
`test-app-launch.sh`, `test-backstack.sh`, `test-install-ux.sh`, `visual-smoke.sh`).

Next priorities, in order:

### P1 — JIT Stability (elfuse/HVF crash) — **COMPLETE (2026-09-05)**
Resolved by capping ART's JIT code cache to 1 MiB (`-Xjitmaxsize:1m`,
`-Xjitinitialsize:512k`, `-Xjitthreshold:200`) so it never exceeds elfuse's
pre-mapped 2 MiB RX window (`MMAP_RX_BASE=0x10000000` .. `MMAP_RX_INITIAL_END=0x10200000`).
Touches and lifecycle methods now execute compiled code without crashing or ~500% CPU spikes.

### P2 — Real Android Task / Back-Stack — **COMPLETE (2026-09-05)**
Replaced the flat host-simulated tab map with a real `TaskRecord` back-stack model:
- `TaskRecord` maintains an `activityStack` (`ArrayDeque<ActivityRecord>`) per task.
- `MuplarServices` intercepts `startActivity` and `finishActivity`/`finishActivityAffinity` via `IActivityTaskManager`.
- Intra-app `startActivity` pauses the top activity and pushes the new activity onto the same task/tab stack.
- `finishActivityByToken` pops the activity; if the task still contains activities, the new top activity is resumed (`onRestart`/`onStart`/`onResume`) without closing the tab or sending `tab-finished`.
- When the task becomes empty, `tab-finished` is dispatched to the host, resuming the previous task (e.g. Launcher3).
- Implemented missing `android.view.KeyEvent` native methods (`nativeNextId`, `nativeKeyCodeToString`, `nativeKeyCodeFromString`) in ART preload shim.
- Verified end-to-end via `platform/android-aarch64/compat/launcher3/test-backstack.sh`.

### P3 — App Install UX — **COMPLETE (2026-09-05)**
Implemented full in-session application installation flow:
- **Host Drag-and-Drop**: `AndroidDeviceFrameView` conforms to `NSDraggingDestination` for `NSPasteboardTypeFileURL`, with visual blue-accent drop-target overlay rendering on drag enter/exit, validating `.apk` extensions and routing directly to the install handler.
- **In-Window Sheet File Picker**: Toolbar "Install APK" button (`square.and.arrow.down`) opens `NSOpenPanel` as an in-window sheet modal (`beginSheetModalForWindow:completionHandler:`), keeping focus inside the device window.
- **Install Progress & Feedback**: In-session progress indicator and completion feedback displayed over the phone viewport during installation.
- **Prefix APK Management**: Automatically stages `.apk` files into `packages_dir`, extracts metadata via aapt/manifest parser, updates `android-packages.properties`, and rescans installed applications.
- **Real-Time Notification Pipeline**: Host dispatches `action=package-installed` through `muplard`, which broadcasts to active guest runtimes.
- **Dynamic Launcher3 App Drawer Refresh**: `MuplarContext.java` binds `LauncherApps` to `ILauncherApps$Stub.asInterface(MuplarServices.getBinder("launcherapps"))`, enabling Launcher3's `LauncherAppState` to register its `IOnAppsChangedListener`. `FrameworkDeviceController` routes `package-installed` to `MuplarServices.notifyPackageAdded(packageName)`, dispatching `onPackageAdded(user, packageName)` directly to Launcher3 in real time without restarting the session.
- Verified end-to-end via `platform/android-aarch64/compat/launcher3/test-install-ux.sh`.

### P4 — HWUI / Framework-Backed Rendering (**CURRENT FOCUS**)
Replace the `MuplarFramePresenter` software-bitmap bridge with real
`ViewRootImpl` / `Surface` semantics so framework rendering naturally posts
frames into `HostWindow`. This removes the 200ms polling loop and enables
smooth, choreographer-timed frame delivery.
