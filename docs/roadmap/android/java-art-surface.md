# Java And ART Surface Checklist

Stable area: Java-facing objects, JNI behavior, Android framework methods, and the ART-facing compatibility layer.

---

## 1. Current State & What Works

- [x] **ART Bootstrapping**: `app_process64` launches `com.muplar.runtime.ArtApkMain` entrypoint with custom bootclasspath (`muplar-art-bootstrap.jar`).
- [x] **APK ClassLoader**: `ArtApkMain` loads APK DEX files and resolves the manifest launch Activity class via `PathClassLoader`.
- [x] **Genuine AOSP SQLite JNI**: `muplar_android_art_shim.c` loads `libandroid_runtime.so` and registers genuine CursorWindow and SQLite natives.
- [x] **Activity Lifecycle Driving**: `ArtApkMain` invokes Activity constructors, attaches context, and drives `onCreate()`, `onStart()`, `onResume()`, and `makeVisible()`.
- [x] **Synthetic Context (`MuplarContext.java`)**: Implements `ContextWrapper` providing system service dispatch, package metadata, resource resolution, and asset extraction.
- [x] **Networking & Jobs Bootstrap**: Implemented `ConnectivityManager` and `MuplarJobScheduler` in `java-bootstrap`.

---

## 2. Active Blockers & Gaps

### Blocker 1: Unpopulated `ActivityThread.mBoundApplication` (P0)
- **Problem**: `ActivityThread` is instantiated reflectively via `allocateWithoutConstructor()`. `mBoundApplication` is `null`.
- **Impact**: Any app calling `Application.getProcessName()` (e.g. WorkManager in F-Droid) crashes with `NullPointerException: getProcessName() must not be null`.
- **Action**: Allocate `ActivityThread$AppBindData` during `installActivityThreadForFramework()`, set `processName = packageName`, and attach `applicationInfo`.

### Blocker 2: Broken Frame Presenter Output (P0)
- **Problem**: `MuplarFramePresenter` uses a 200ms software DecorView snapshot hack (`decor.draw(canvas)`) which frequently produces black, blank, or frozen frames.
- **Impact**: Even when an Activity reaches `onResume` and inflates views, the macOS window shows a blank screen.
- **Action**: Audit `MuplarFramePresenter.drawViewToBitmap()` and native frame dumping to ensure real rendered pixels reach the display.

### Blocker 3: Unbacked `LauncherApps` Query (P1)
- **Problem**: `MuplarServices.launcherAppsValue()` returns dummy data.
- **Impact**: Launcher3's workspace and all-apps drawer remain empty.
- **Action**: Back `LauncherApps` with installed APK manifests.

---

## 3. Long-Term Architecture

- [ ] Transition from 200ms software bitmap capture to direct `ViewRootImpl` / `Surface` / Metal texture sharing.
- [ ] Add `InputMethodManager` stubs and virtual/hardware keyboard event bridge.
- [ ] Implement inter-app `startActivity` and real `ActivityManager` task back-stack management.
