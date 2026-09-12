# Launcher3 Status & Compatibility Target

Launcher3 is the compatibility target for Android home-screen behavior in Muplar.

---

## 1. Ground Truth & Current Reality

Although headless test logs show `onResume completed successfully` and view hierarchy inflation (`FrameLayout -> LauncherRootView -> DragLayer`), **Launcher3 is currently non-functional for real interactive use**:

- 🔴 **Empty Home Screen & App Drawer**: No installed applications appear on the home screen or app drawer because `MuplarServices.launcherAppsValue()` does not query real installed APK manifest data.
- 🔴 **Touch Input Does Not Work**: Mouse clicks and pointer events forwarded from `AndroidDeviceShell` do not trigger icon clicks, long presses, or view interactions.
- 🔴 **Back Button Looper Hang**: Pressing Back on `QuickstepLauncher` enters an infinite hang in `onBackPressed()`, causing the entire session to become unresponsive.
- 🔴 **Frame Presentation Failure**: The 200ms software DecorView snapshot path in `MuplarFramePresenter` frequently delivers black, blank, or frozen frames to `HostWindow`.

---

## 2. Resolved Items & Progress

- [x] **ART JIT Stability (`art_bootstrap.cpp`)**:
  - Capped ART's JIT code cache to 1 MiB (`-Xjitmaxsize:1m`, `-Xjitinitialsize:512k`, `-Xjitthreshold:200`) so it does not overflow elfuse's pre-mapped 2 MiB RX window (`0x10000000`–`0x10200000`), eliminating HVF W^X translation fault panics.
- [x] **Genuine SQLite & CursorWindow Natives (`muplar_android_art_shim.c`)**:
  - Bound genuine AOSP SQLite registration symbols from `libandroid_runtime.so` (`register_android_database_CursorWindow`, `SQLiteConnection`, `SQLiteGlobal`, `SQLiteDebug`, `SQLiteRawStatement`, `SQLiteUserDataRecovery`).
  - Removed mock JNI stubs that hardcoded Launcher3 favorites table columns.
- [x] **IPC Native Socket Bridge (`MuplarSocketClient.java`)**:
  - Replaced guest `ProcessBuilder` execution with direct `AF_UNIX` socket IPC, avoiding elfuse `execve` failures.
- [x] **Process Cleanup Hardening (`PrefixManagerApp.mm`)**:
  - Added 500ms `SIGKILL` fallback when closing the session window, preventing orphaned `mup` and `muplard` background processes.

---

## 3. Active Blockers & Execution Priorities

### Blocker 1: Unbacked `LauncherApps` Installed-App Query
- **Problem**: Launcher3 queries `ILauncherApps` to populate workspace icons and the all-apps drawer. `MuplarServices.java` currently returns empty or hardcoded lists.
- **Fix**: Parse installed APK manifests in `~/.muplar/prefixes/<name>/packages/` and dynamically populate `LauncherApps.getActivityList()`.

### Blocker 2: Touch Input Routing & View Dispatch
- **Problem**: Clicks in `AndroidDeviceShell` pass into `muplard`, but `FrameworkDeviceController` either fails to construct valid `MotionEvent` instances or fails to deliver them to the targeted child view.
- **Fix**: Trace `AndroidDeviceShell.mm` coordinates -> `FrameworkDeviceController.dispatchTouchEvent()` -> `LauncherRootView.dispatchTouchEvent()`. Ensure taps produce visible icon focus and press states.

### Blocker 3: Back Button Session Hang
- **Problem**: Calling `onBackPressed()` on `QuickstepLauncher` enters an unhandled looper wait, wedging the entire guest session.
- **Fix**: Ensure `onBackPressed()` does not block on missing window animations or unhandled Choreographer callbacks. Provide an immediate return or no-op when already at the Home workspace root.

### Blocker 4: Real Frame Delivery via Presenter
- **Problem**: `MuplarFramePresenter` frequently fails to capture valid pixels or captures an empty black surface.
- **Fix**: Audit `MuplarFramePresenter.drawViewToBitmap()` to ensure the active decor view is valid, dirty flags are respected, and pixel data is written to the MHR frame.

---

## 4. Verification Commands

```sh
# Build mup and runtime bundle
PATH="/opt/homebrew/bin:$PATH" ninja -C build

# Run headless lifecycle smoke test
platform/android-aarch64/compat/launcher3/smoke-launch.sh

# Capture visual frame to verify non-black pixels
platform/android-aarch64/compat/launcher3/visual-smoke.sh
```
