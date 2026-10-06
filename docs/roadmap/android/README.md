# Android Roadmap & Subsystem Status

This document tracks the current ground truth, architectural status, and engineering milestones for the Muplar Android ARM64 runtime.

---

## 1. Ground-Truth Reality Check

Muplar runs an enlightened Android 15 (API 35) ARM64 ART userland environment on macOS via Apple's Hypervisor.framework (HVF) and `elfuse`. 

The interactive device experience across the core loop is now **functioning and verified end-to-end**:
- **Display & Frame Presentation**: 🟢 **Functional**. Event-driven software DecorView presentation is active, reliably presenting 1080x1920 multi-color frames to the host shell (`visual-smoke.sh`, `test-framework-rendering.sh`).
- **Touch & Pointer Input**: 🟢 **Working**. Pointer events from `AndroidDeviceShell` via `muplard` translate accurately and dispatch into `dispatchTouchEvent()`, triggering button clicks and view transitions (`test-touch-interaction.sh`).
- **Home Launcher (Launcher3)**: 🟢 **Working**. Launcher3 binds installed applications dynamically via manifest auto-sync, responds to drawer drag gestures, launches apps on icon tap, and cleanly handles Back navigation without looper deadlocks (`test-click-icon.sh`, `test-launcher-drag.sh`, `test-app-drawer.sh`).
- **Application Lifecycle & Back-Stack**: 🟢 **Working**. Multi-activity task back-stacks, in-session dynamic APK installation, and custom `Application` subclass instantiation are verified (`test-app-launch.sh`, `test-backstack.sh`, `test-install-ux.sh`).

---

## 2. Component Status Matrix

| Subsystem | Component | Status | Reality Summary | Detailed Document |
| :--- | :--- | :---: | :--- | :--- |
| **Display / Window** | `BLASTBufferQueue` & Presenter | 🟢 **Functional** | Event-driven software DecorView presentation active; valid visual frames verified. | [device-window.md](./device-window.md) |
| **Touch Input** | `MotionEvent` & Touch Routing | 🟢 **Working** | Pointer events translated and dispatched cleanly to focused View/Activity. | [device-window.md](./device-window.md) |
| **Home Launcher** | `Launcher3` | 🟢 **Working** | App icons bound dynamically, drawer gesture working, Back navigation stable. | [launcher3.md](./launcher3.md) |
| **Target App** | `UiTest` / Applications | 🟢 **Working** | Multi-activity task back-stack, in-session install UX, and custom Application lifecycle verified. | [framework-services.md](./framework-services.md) |
| **Database** | SQLite & CursorWindow | 🟢 **Working** | Genuine AOSP `libandroid_runtime.so` SQLite symbols bound and persisting. | [framework-services.md](./framework-services.md) |
| **App Identity** | `ActivityThread` / `AppBindData` | 🟢 **Working** | `mBoundApplication` populated, `Process.sProcessName` set, manifest Application class resolved. | [java-art-surface.md](./java-art-surface.md) |
| **Network & Jobs** | `ConnectivityManager` & `JobScheduler` | 🟡 **Stubbed** | Bootstrap classes added for WorkManager initialization. | [framework-services.md](./framework-services.md) |
| **Host Bridge** | `MuplarSocketClient` | 🟢 **Working** | Direct `AF_UNIX` socket IPC replaces guest `execve`. | [runtime.md](./runtime.md) |
| **Process Model** | `mup` / `muplard` | 🟢 **Working** | 500ms `SIGKILL` fallback terminates rogue tasks on exit; package registry auto-synced. | [device-window.md](./device-window.md) |
| **Kernel / HVF** | `elfuse` ARM64 VM | 🟢 **Stable** | JIT capped to 1 MiB (`-Xjitmaxsize:1m`) preventing W^X collisions with elfuse RX window. | [runtime.md](./runtime.md) |

---

## 3. Engineering Milestones Status

### Milestone 1 (P0): Fix App Identity & Stop WorkManager Crash
- [x] Populate `ActivityThread.mBoundApplication` with `AppBindData` (`processName = packageName`).
- [x] Set `ApplicationInfo.processName` and reflectively set `Process.sProcessName`.
- [x] Resolve `Application` subclass directly from `AndroidManifest.xml` when not pre-registered.

### Milestone 2 (P0): Fix Frame Presentation Pipeline
- [x] Fixed `MuplarFramePresenter` starvation and deadlock in looper.
- [x] Ensure the active foreground DecorView is actively drawn and valid pixels reach macOS `HostWindow`.
- [x] Automated visual smoke tests pass with valid multi-color UI frames (`visual-smoke.sh`, `test-framework-rendering.sh`).

### Milestone 3 (P1): Fix Touch Input Routing End-to-End
- [x] Pointer events from `AndroidDeviceShell.mm` through `muplard` `DeviceInput` to `dispatchTouchEvent()`.
- [x] Coordinate translation mapped with default 1080x1920 device frame resolution.
- [x] Taps and gestures produce state transitions and button clicks (`test-touch-interaction.sh`, `test-click-icon.sh`).

### Milestone 4 (P1): Fix Launcher3 App Listing & Back-Button Hang
- [x] Auto-populate `android-packages.properties` from installed APKs in `packages/`.
- [x] Fixed `onBackPressed()` looper hang with state-aware navigation back to NORMAL state.
- [x] In-session app install UX dynamically refreshes Launcher3 (`test-install-ux.sh`).

### Milestone 5 (P2): Hardware Accelerated Rendering & Extended Services
- [ ] Hardware-accelerated HWUI rendering via ANGLE/Metal backing `BLASTBufferQueue`.
- [ ] InputMethodManager (IME) software keyboard integration for text input.
- [ ] Extended NotificationManager and AlarmManager service delivery.

---

## 4. Supporting Documentation Directory

- [Android Device Window (`device-window.md`)](./device-window.md) — Shell, session controller, and frame presentation.
- [Launcher3 Status (`launcher3.md`)](./launcher3.md) — Home launcher compatibility, blockers, and lifecycle.
- [Framework Services Architecture (`framework-services.md`)](./framework-services.md) — Daemon services, SQLite, Connectivity, and JobScheduler.
- [Java and ART Surface (`java-art-surface.md`)](./java-art-surface.md) — JNI bindings, class loader, and `ArtApkMain`.
- [Runtime Engine (`runtime.md`)](./runtime.md) — HVF execution, Bionic sysroot, and socket IPC bridge.
- [Binder and AIDL (`binder-aidl.md`)](./binder-aidl.md) — IPC transport, Parcel serialization, and NDK Binder.
- [APK Dependencies (`apk-dependencies.md`)](./apk-dependencies.md) — Relocations, native libraries, and dependency closure.
- [Android Sysroot (`sysroot.md`)](./sysroot.md) — API 35 sysroot composition and framework jar imports.
- [Compatibility Scanning (`compatibility-scanning.md`)](./compatibility-scanning.md) — Static inspection and API stub inventory.
