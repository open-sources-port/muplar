# Java And ART Surface Checklist

Stable area: Java-facing objects, JNI behavior, Android framework methods, and the ART-facing compatibility layer.

---

## 1. Current State & What Works

- [x] **ART Bootstrapping**: `app_process64` launches `com.muplar.runtime.ArtApkMain` entrypoint with custom bootclasspath (`muplar-art-bootstrap.jar`).
- [x] **APK ClassLoader**: `ArtApkMain` loads APK DEX files and resolves the manifest launch Activity class via `PathClassLoader`.
- [x] **Genuine AOSP SQLite JNI**: `muplar_android_art_shim.c` loads `libandroid_runtime.so` and registers genuine CursorWindow and SQLite natives.
- [x] **Activity Lifecycle Driving**: `ArtApkMain` invokes Activity constructors, attaches context, and drives `onCreate()`, `onStart()`, `onResume()`, and `makeVisible()`.
- [x] **Synthetic Context (`MuplarContext.java`)**: Implements `ContextWrapper` providing system service dispatch, package metadata, resource resolution, and asset extraction.
- [x] **App Identity & WorkManager Support**:
  - Populates `ActivityThread$AppBindData` with `mBoundApplication` during `installActivityThreadForFramework()`.
  - Sets `applicationInfo.processName` and `Process.sProcessName` to prevent `NullPointerException: getProcessName() must not be null`.
  - Reflectively resolves custom `<application android:name="...">` subclasses from `AndroidManifest.xml`.
- [x] **Frame Presenter Stabilization**:
  - `MuplarFramePresenter` captures valid multi-color UI pixels from the active DecorView and delivers them to the host window.
- [x] **LauncherApps Installed-App Query**:
  - Backed by dynamic manifest parsing from `packages/` directory, populating Launcher3 icons and dispatching `onPackageAdded`.
- [x] **Networking & Jobs Bootstrap**: Implemented `ConnectivityManager` and `MuplarJobScheduler` in `java-bootstrap`.

---

## 2. Active Focus & Next Steps

### Priority 1: Text Input & InputMethodManager
- **Problem**: Key input currently delivers raw hardware key events. Typing into `EditText` requires IME or `InputConnection` support for character composition.
- **Action**: Implement `InputMethodManager` stubs and character input bridging in `java-bootstrap`.

### Priority 2: Extended Framework Services
- **Problem**: `NotificationManager` and `AlarmManager` are minimal in-memory stubs.
- **Action**: Implement host notification forwarding and scheduled alarm execution via `muplard`.

---

## 3. Long-Term Architecture

- [ ] Transition from software bitmap capture to direct `ViewRootImpl` / `Surface` / Metal texture sharing.
- [ ] Implement inter-app `startActivity` and real `ActivityManager` task back-stack management.
