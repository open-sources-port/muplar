# Launcher3 Status & Compatibility Target

Launcher3 is the compatibility target for Android home-screen behavior in Muplar.

---

## 1. Ground Truth & Current Reality

Launcher3 is **functional and verified for interactive use**:

- 🟢 **Dynamic App Binding**: Installed applications appear dynamically on the home screen and in the all-apps drawer via manifest synchronization (`test-app-drawer.sh`, `test-install-ux.sh`).
- 🟢 **Touch & Click Dispatch**: Pointer events translated from `AndroidDeviceShell` through `muplard` dispatch into `dispatchTouchEvent()`, triggering icon launches and drawer drag transitions (`test-touch-interaction.sh`, `test-click-icon.sh`).
- 🟢 **Back Button Stability**: Back navigation in `QuickstepLauncher` uses state-aware navigation without entering unhandled looper deadlocks (`test-backstack.sh`).
- 🟢 **Frame Presentation**: Event-driven software DecorView snapshots reliably present valid 1080x1920 frames to `HostWindow` (`visual-smoke.sh`, `test-framework-rendering.sh`).

---

## 2. Resolved Items & Completed Milestones

- [x] **Dynamic `LauncherApps` Installed-App Query**:
  - Auto-populates `android-packages.properties` from installed APKs in `~/.muplar/prefixes/<name>/packages/`.
  - Dispatches `onPackageAdded` notifications to `LauncherApps.Callback` on dynamic package installation.
- [x] **Touch Input Routing (`MotionEvent.nativeInitialize`)**:
  - Bound genuine `MotionEvent` native allocation and field accessors in `muplar_android_art_shim.c`.
  - Scaled and mapped macOS window coordinates (1080x1920) through `FrameworkDeviceController`.
- [x] **Back Button Navigation**:
  - State-aware `performBack()` returns to normal home workspace state or finishes current task without wedging the looper.
- [x] **Frame Presenter Stabilization**:
  - Resolved main-looper starvation; `MuplarFramePresenter` captures valid multi-color UI frames and writes to MHR frame transport.
- [x] **ART JIT Stability (`art_bootstrap.cpp`)**:
  - Capped ART's JIT code cache to 1 MiB (`-Xjitmaxsize:1m`, `-Xjitinitialsize:512k`, `-Xjitthreshold:200`) so it does not overflow elfuse's pre-mapped 2 MiB RX window (`0x10000000`–`0x10200000`), eliminating HVF W^X translation fault panics.
- [x] **Genuine SQLite & CursorWindow Natives (`muplar_android_art_shim.c`)**:
  - Bound genuine AOSP SQLite registration symbols from `libandroid_runtime.so` (`register_android_database_CursorWindow`, `SQLiteConnection`, `SQLiteGlobal`, `SQLiteDebug`, `SQLiteRawStatement`, `SQLiteUserDataRecovery`).
  - Removed mock JNI stubs that hardcoded Launcher3 favorites table columns.
- [x] **IPC Native Socket Bridge (`MuplarSocketClient.java`)**:
  - Replaced guest `ProcessBuilder` execution with direct `AF_UNIX` socket IPC, avoiding elfuse `execve` failures.
- [x] **Trackpad & Mouse Scroll Wheel Support (`AndroidDeviceShell.mm`)**:
  - Implemented `scrollWheel:` event mapping translating macOS trackpad phases and mouse wheel deltas into smooth Android pointer drag sequences.
- [x] **Process Cleanup Hardening (`PrefixManagerApp.mm`)**:
  - Added 500ms `SIGKILL` fallback when closing the session window, preventing orphaned `mup` and `muplard` background processes.

---

## 3. Active Focus & Next Enhancements

### Priority 1: Text Input & IME Integration
- Support software keyboard and text input events for Launcher3 search bar and app inputs.

### Priority 2: Hardware-Accelerated Rendering (P2)
- Transition from software DecorView MHR frame capture to hardware-accelerated ANGLE/Metal backing `BLASTBufferQueue`.

---

## 4. Verification Commands

```sh
# Build mup and runtime bundle
PATH="/opt/homebrew/bin:$PATH" ninja -C build

# Run headless lifecycle smoke test
platform/android-aarch64/compat/launcher3/smoke-launch.sh

# Capture visual frame to verify non-black pixels
platform/android-aarch64/compat/launcher3/visual-smoke.sh

# Run end-to-end interactive compat suite
platform/android-aarch64/compat/launcher3/test-touch-interaction.sh
platform/android-aarch64/compat/launcher3/test-app-drawer.sh
platform/android-aarch64/compat/launcher3/test-click-icon.sh
platform/android-aarch64/compat/launcher3/test-backstack.sh
platform/android-aarch64/compat/launcher3/test-install-ux.sh
```
