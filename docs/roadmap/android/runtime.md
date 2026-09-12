# Android Runtime Checklist

Stable area: Native execution, JNI bindings, windowing, input routing, process lifecycle, and ART/HVF integration.

---

## 1. Done

- [x] Direct Android `.so` execution through `JNI_OnLoad` and `RegisterNatives`.
- [x] ART execution via `app_process64` inside an Android API 35 ARM64 sysroot.
- [x] Genuine AOSP SQLite and CursorWindow native binding via `libandroid_runtime.so`.
- [x] Native socket bridge (`MuplarSocketClient.java` + JNI) communicating over `AF_UNIX` without guest `execve`.
- [x] JIT code cache tuning (`-Xjitmaxsize:1m`) preventing HVF W^X page-table collision with elfuse RX window.
- [x] Process lifecycle cleanup in `PrefixManagerApp.mm` with 500ms `SIGKILL` fallback.
- [x] APK launch envelope, asset extraction, `AAssetManager`, and manifest parsing.
- [x] AppKit device window (`AndroidDeviceShell`) with toolbar navigation controls.

---

## 2. Active Blockers & In Progress

- [ ] **Frame Presentation Broken**:
  - `BLASTBufferQueue` is disconnected from host.
  - `MuplarFramePresenter` software DecorView snapshot path frequently outputs black or blank frames.
- [ ] **Touch Input Routing Broken**:
  - Pointer events from `AndroidDeviceShell` fail to trigger clicks, view state changes, or scrolling.
- [ ] **Back-Button Looper Hang**:
  - Calling `onBackPressed()` on `QuickstepLauncher` enters an infinite looper hang, locking the session.
- [ ] **WorkManager Crash**:
  - `Application.getProcessName()` returns `null` because `ActivityThread.mBoundApplication` is unpopulated.

---

## 3. Next Steps

- [ ] Diagnose and fix `MuplarFramePresenter` to reliably output active DecorView pixels to `HostWindow`.
- [ ] Connect mouse clicks from `AndroidDeviceShell` end-to-end to `dispatchTouchEvent()`.
- [ ] Populate `ActivityThread.mBoundApplication` to stop WorkManager startup crashes.
- [ ] Provide real `LauncherApps` query data from installed APK manifests.
