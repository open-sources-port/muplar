# Android Roadmap & Subsystem Status

This document tracks the current ground truth, architectural status, and engineering milestones for the Muplar Android ARM64 runtime.

---

## 1. Ground-Truth Reality Check

Muplar runs an enlightened Android 15 (API 35) ARM64 ART userland environment on macOS via Apple's Hypervisor.framework (HVF) and `elfuse`. 

While core process launching, IPC socket communication, and AOSP SQLite native bindings are functioning, **the interactive device experience is currently broken**:
- **Display & Frame Presentation**: 🔴 **Broken**. Android's internal `BLASTBufferQueue` does not route frames to the host. The current `MuplarFramePresenter` is a 200ms software DecorView bitmap snapshot hack that frequently dumps black, blank, or frozen frames. Real HWUI/Metal surface presentation is not implemented.
- **Touch & Pointer Input**: 🔴 **Broken**. Pointer events sent from `AndroidDeviceShell` via `muplard` do not reliably trigger button clicks, view interactions, or list scrolling.
- **Launcher3 (Home)**: 🔴 **Broken**. Although guest ART reaches `onResume`, the screen is empty (unbacked `LauncherApps` query), touch is dead, and pressing the Back button hangs the session looper.
- **Third-Party Applications (F-Droid)**: 🔴 **Crashes on Launch**. Fails during startup due to `NullPointerException: getProcessName() must not be null` in WorkManager.

---

## 2. Component Status Matrix

| Subsystem | Component | Status | Reality Summary | Detailed Document |
| :--- | :--- | :---: | :--- | :--- |
| **Display / Window** | `BLASTBufferQueue` & Presenter | 🔴 **Broken** | Software snapshot hack; blank/black frames; no Metal pipeline. | [device-window.md](./device-window.md) |
| **Touch Input** | `MotionEvent` & Touch Routing | 🔴 **Broken** | Clicks in `AndroidDeviceShell` do not interact with views. | [device-window.md](./device-window.md) |
| **Home Launcher** | `Launcher3` | 🔴 **Broken** | Empty screen, unbacked app query, Back button looper hang. | [launcher3.md](./launcher3.md) |
| **Target App** | `F-Droid` | 🔴 **Crashing** | Crashes on startup (`getProcessName()` null in WorkManager). | [framework-services.md](./framework-services.md) |
| **Database** | SQLite & CursorWindow | 🟡 **Bound** | Genuine AOSP `libandroid_runtime.so` SQLite symbols bound. | [framework-services.md](./framework-services.md) |
| **App Identity** | `ActivityThread` / `AppBindData` | 🔴 **Broken** | `mBoundApplication` null; `getProcessName()` returns null. | [java-art-surface.md](./java-art-surface.md) |
| **Network & Jobs** | `ConnectivityManager` & `JobScheduler` | 🟡 **Stubbed** | Bootstrap classes added for WorkManager initialization. | [framework-services.md](./framework-services.md) |
| **Host Bridge** | `MuplarSocketClient` | 🟡 **Working** | Direct `AF_UNIX` socket IPC replaces broken guest `execve`. | [runtime.md](./runtime.md) |
| **Process Model** | `mup` / `muplard` | 🟡 **Working** | 500ms `SIGKILL` fallback terminates rogue tasks on exit. | [device-window.md](./device-window.md) |
| **Kernel / HVF** | `elfuse` ARM64 VM | 🟡 **Precarious** | JIT capped to 1 MiB (`-Xjitmaxsize:1m`) to avoid W^X panics. | [runtime.md](./runtime.md) |

---

## 3. Immediate Engineering Milestones

### Milestone 1 (P0): Fix App Identity & Stop WorkManager Crash
- Populate `ActivityThread.mBoundApplication` with `AppBindData` (`processName = packageName`).
- Set `ApplicationInfo.processName` and reflectively set `Process.sProcessName`.
- Verify `F-Droid` and `Launcher3` background workers start without throwing `NullPointerException`.

### Milestone 2 (P0): Fix Frame Presentation Pipeline
- Diagnose why `MuplarFramePresenter` yields blank/black screens.
- Ensure the active foreground DecorView is actively drawn and valid pixels reach macOS `HostWindow`.
- Establish an automated visual verification test that asserts non-blank framebuffer pixels.

### Milestone 3 (P1): Fix Touch Input Routing End-to-End
- Trace mouse clicks from `AndroidDeviceShell.mm` through `muplard` `DeviceInput` to `dispatchTouchEvent()`.
- Fix coordinate translation between macOS window points and Android display resolution (`1080x1920`).
- Ensure button taps produce visible UI state transitions.

### Milestone 4 (P1): Fix Launcher3 App Listing & Back-Button Hang
- Back `LauncherApps` query with installed APK manifest metadata so apps appear on the desktop.
- Fix the `onBackPressed()` looper hang.

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
