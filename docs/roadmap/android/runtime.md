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
- [x] Frame presentation pipeline (`MuplarFramePresenter` DecorView snapshot delivering valid frames).
- [x] Touch input routing end-to-end (pointer events from `AndroidDeviceShell` to `FrameworkDeviceController` and view dispatch).
- [x] Back-button looper hang resolution (`performBack()` state-aware navigation on `QuickstepLauncher`).
- [x] App identity & WorkManager stability (`ActivityThread.mBoundApplication` populated + manifest `Application` resolution).
- [x] Installed apps query (`sync_packages_registry` from prefix `packages/` directory).

---

## 2. Active Focus & Next Steps

- [ ] Connect Metal hardware surface pipeline to replace software DecorView frame presentation.
- [ ] Add support for multi-window / freeform window management mode.
- [ ] Implement audio track forwarding via host CoreAudio bridge.
