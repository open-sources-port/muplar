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

Status: partially implemented. `AndroidDeviceShell` now owns a host-managed tab
strip, starts with a persistent Launcher tab, focuses existing tabs on repeated
launches, and lets app tabs close back to Launcher or the previous tab. App tab
focus now sends staged APK path, package, and launch Activity metadata through
`muplard`; the Java bootstrap can load that APK and start its Activity inside
the existing ART process. Full Android task/back-stack modeling is still
pending.

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

Status: partially implemented. The device toolbar now has host-side Back,
Home, Recents, Settings, Install APK, Turn Off, and Close Active Tab actions.
Home and Recents operate on the host tab model, Settings opens a host tab, and
all actions are reported back to Instance Manager, appended to the per-prefix
log, and sent to `muplard` through the `DeviceAction` protocol. `muplard`
validates and records the latest requested action so the runtime has a
prefix-scoped control bridge. The Java bootstrap subscribes to that action
stream once per APK process and applies actions to the current Activity: Back
invokes app back handling, Home pauses/stops the activity, app-tab focus loads
or resumes the requested app Activity, and app-tab close finishes/destroys the
current Activity. True Android task/back-stack semantics are still pending.

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

Status: implemented for the current software-rendered Launcher3 path.
`HostWindow` can stream rendered RGBA frames over a per-prefix Unix socket when
launched from Instance Manager, and `AndroidDeviceShell` receives those frames
inside its reserved phone screen area. Native/EGL and Java software-presented
frames no longer need a separate standalone host window in this path. Java
Views render through `MuplarFramePresenter`: the active decor view is drawn to
a software `Bitmap`, serialized as a raw MHR frame in the prefix rootfs, then
picked up by `HostWindow` and forwarded into the embedded device screen. Basic
pointer/key forwarding from the embedded screen back into the runtime is
implemented, including macOS to Android key-code translation for common
keyboard input. The device shell also forwards those events through `muplard`
to the Java bootstrap, where they are dispatched on the main looper to the
active Activity as `MotionEvent` and `KeyEvent` objects.

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

Touch input is the top priority: it reaches `MotionEvent`/`dispatchTouchEvent`
on the real Launcher3 but crashes the guest natively before returning — see
"Main Blockers" in [Launcher3 Status](./launcher3.md) for the current
diagnosis. A device that can't reliably take touch input doesn't feel like
BlueStacks/MuMuPlayer no matter how solid the surrounding tab/session UI is,
so this blocks the core product goal directly.

After that: Android task/back-stack handling and Step 5. The host shell,
prefix-scoped process ownership, tab UI, toolbar action callbacks, `muplard`
action/input delivery, Java-side current-activity control, and in-process APK
Activity launching now exist. Next priorities are Android task/back-stack
state, app install UX, and replacing the software Java frame bridge with
framework/HWUI-backed presentation.
