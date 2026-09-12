# Android Framework Services Architecture

Muplar hosts shared Android framework services and system manager shims through a hybrid architecture combining the `muplard` daemon, the Java bootstrap (`muplar-art-bootstrap.jar`), and native ART shim hooks (`libmuplar_android_art_shim.so`).

---

## 1. Recent Accomplishments

### Genuine Android SQLite & CursorWindow Natives
- **Problem**: Previous mock SQLite JNI implementations hardcoded Launcher3 favorites table columns. When modern apps (such as F-Droid, Room, or WorkManager) queried their databases, Room's schema verification threw:
  `IllegalStateException: Pre-packaged database has an invalid schema: Expected: columns={...}, Found: columns={}`.
- **Resolution**:
  - Bound genuine AOSP SQLite registration symbols from `/system/lib64/libandroid_runtime.so`:
    - `_ZN7android38register_android_database_CursorWindowEP7_JNIEnv`
    - `_ZN7android42register_android_database_SQLiteConnectionEP7_JNIEnv`
    - `_ZN7android38register_android_database_SQLiteGlobalEP7_JNIEnv`
    - `_ZN7android37register_android_database_SQLiteDebugEP7_JNIEnv`
    - `_ZN7android44register_android_database_SQLiteRawStatementEP7_JNIEnv`
    - `_ZN7android48register_android_database_SQLiteUserDataRecoveryEP7_JNIEnv`
  - Removed mock JNI stubs from `JNI_OnLoad`.
  - Room and WorkManager SQLite queries now execute against genuine Android SQLite.

### Networking Stack Implementation (`android.net`)
- Created real Java bootstrap implementations in `platform/android-aarch64/java-bootstrap/android/net/`:
  - `ConnectivityManager.java`: Network callbacks, active network queries.
  - `Network.java`, `NetworkCapabilities.java`, `NetworkInfo.java`, `NetworkRequest.java`, `LinkProperties.java`.
- Enables WorkManager's `NetworkStateTracker24` to initialize without `NullPointerException`.

### JobScheduler Service (`android.app.job`)
- Implemented `MuplarJobScheduler.java` in Java bootstrap:
  - Supports `schedule()`, `enqueue()`, `cancel()`, and API 34+ `forNamespace()`.
  - Unblocks WorkManager's `SystemJobScheduler` and `SystemJobService`.

### Context & Power State
- Extended `MuplarContext.java`:
  - Added `isDeviceProtectedStorage()`, `startActivity(Intent, Bundle)`.
  - Added URI permission stubs (`grantUriPermission`, `revokeUriPermission`, `checkUriPermission`).
  - Added sticky `Intent.ACTION_BATTERY_CHANGED` broadcast handling in `registerReceiver`.

---

## 2. Active Blocker: WorkManager Process Name NullPointerException

### Symptom
When F-Droid or apps using AndroidX WorkManager start up:
```
[Muplar/ART] Application onCreate failed: java.lang.NullPointerException: getProcessName() must not be null
java.lang.NullPointerException: getProcessName() must not be null
	at androidx.work.impl.WorkManagerImpl.<init>(WorkManagerImpl.java:261)
	at androidx.work.impl.WorkManagerImplExtKt.createWorkManager(WorkManagerImplExt.kt:58)
	at org.fdroid.fdroid.FDroidApp.onCreate(FDroidApp.java:357)
```

### Root Cause
1. AndroidX WorkManager calls `Application.getProcessName()` during initialization.
2. In AOSP, `Application.getProcessName()` delegates to `ActivityThread.currentProcessName()`.
3. `ActivityThread.currentProcessName()` looks up `ActivityThread.currentActivityThread().mBoundApplication.processName`.
4. In `ArtApkMain.java`, `ActivityThread` is instantiated via `allocateWithoutConstructor()`, leaving `mBoundApplication` as `null`.
5. In addition, `ApplicationInfo.processName` is unpopulated in `MuplarContext.java`.

### Required Fix
1. Instantiate `ActivityThread$AppBindData` during `installActivityThreadForFramework()` and set `processName = packageName`.
2. Populate `mBoundApplication` on the current `ActivityThread`.
3. Set `this.applicationInfo.processName = this.packageName` in `MuplarContext.java`.
4. Reflectively set `Process.sProcessName` and invoke `Process.setProcessName()`.

---

## 3. Framework Service Delivery Roadmap

| Service | Target Package | Status | Priority |
| :--- | :--- | :---: | :---: |
| **SQLite & CursorWindow** | `android.database.sqlite` | ✅ Genuine AOSP Bound | Completed |
| **ConnectivityManager** | `android.net` | 🟡 Bootstrap Stubbed | In Progress |
| **JobScheduler** | `android.app.job` | 🟡 Bootstrap Stubbed | In Progress |
| **App Identity / ProcessName** | `android.app.ActivityThread` | 🔴 Broken (NPE) | **P0 Active** |
| **LauncherApps** | `android.content.pm` | 🔴 Empty / Unbacked | **P1 Next** |
| **InputMethodManager (IME)**| `android.view.inputmethod` | 🔴 Missing | P2 |
| **NotificationManager** | `android.app` | 🟡 Partial Stub | P2 |
| **AlarmManager** | `android.app` | 🟡 Basic Stub | P2 |
| **DownloadManager** | `android.app` | ⏳ Not Started | P3 |
