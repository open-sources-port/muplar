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

## 2. Resolved Blocker: WorkManager Process Name NullPointerException

### Resolution
- Instantiated `ActivityThread$AppBindData` during `installActivityThreadForFramework()` and set `processName = packageName`.
- Populated `mBoundApplication` on the active `ActivityThread`.
- Set `this.applicationInfo.processName = this.packageName` in `MuplarContext.java`.
- Reflectively set `Process.sProcessName` and invoked `Process.setProcessName()`.
- Dynamically extracted custom `<application android:name="...">` subclasses from binary `AndroidManifest.xml` via `openXmlResourceParser`.

---

## 3. Framework Service Delivery Roadmap

| Service | Target Package | Status | Priority |
| :--- | :--- | :---: | :---: |
| **SQLite & CursorWindow** | `android.database.sqlite` | ✅ Genuine AOSP Bound | Completed |
| **App Identity / ProcessName** | `android.app.ActivityThread` | ✅ Resolved & Verified | Completed |
| **LauncherApps** | `android.content.pm` | ✅ Backed by Manifest Auto-Sync | Completed |
| **ConnectivityManager** | `android.net` | 🟡 Bootstrap Stubbed | In Progress |
| **JobScheduler** | `android.app.job` | 🟡 Bootstrap Stubbed | In Progress |
| **NotificationManager** | `android.app` | 🟡 Persistent channel/group metadata; delivery remains incomplete | In Progress |
| **AlarmManager** | `android.app` | 🟡 Basic Stub | In Progress |
| **InputMethodManager (IME)**| `android.view.inputmethod` | 🔴 Missing | P2 |
| **SubscriptionManager** | `android.telephony` | 🟡 No-SIM queries and initial listener lifecycle verified; no modem/SIM provisioning | In Progress |
| **DownloadManager** | `android.app` | ⏳ Not Started | P3 |


## Notification-channel metadata milestone

`MuplarNotificationChannels` implements the public self-package channel/group
metadata operations through the framework's `INotificationManager` interface.
It returns valid `ParceledListSlice` results, stores defensive snapshots, retains
channel behavior on deletion/recreation, and persists package-keyed state in the
instance rootfs using file locks, fsync and atomic rename.

The real-ART smoke test uses two packages and separate guest launches to verify
CRUD, namespace separation and restart persistence. Account and stable-display
regressions also pass. This does not implement notification delivery to macOS,
full caller authorization, multi-user operation or metadata migration across
framework versions.

See [the Play Store plan](./play-store.md) for reproduction commands and the
remaining native Binder/Parcel interoperability work. Play Store now enters the
main looper and survives a 90-second observation with no-SIM subscriptions; the
captured frame is still blank and sign-in remains unverified.

## Manifest and absent-backup milestone

The self-application context reads compiled-manifest application metadata and
application/component themes. The APK loader supports declared legacy HTTP from
the Android image. A missing backup manager is reported as absent, allowing the
real BackupManager frontend to take its unavailable-service path rather than
entering native Binder through a fabricated service.

See the [latest Play Store milestone](./play-store.md#manifest-loading-and-optional-backup-absence)
for scope, real-ART regression commands, and follow-up compatibility work.

## No-SIM subscription milestone

Genuine framework SubscriptionManager and TelephonyRegistryManager frontends use
local subscription/registry interfaces backed by an immutable no-SIM state.
Queries return empty lists and invalid IDs; registration delivers an initial
snapshot through the framework's executor/legacy looper path. Duplicate handling,
removal and re-registration are covered by the real-ART smoke test. Unsupported
mutations fail explicitly; modem events, SIM provisioning, multi-user policy and
cross-process telephony transport remain unsupported.

See the [subscription milestone](./play-store.md#latest-milestone--no-sim-subscriptions)
for scope and reproduction commands. The subsequent
[package-discovery milestone](./play-store.md#manifest-backed-package-discovery--november-25-2026)
records real cross-package versions/signers and the latest native Binder startup
abort; the older blank-screen result is historical.


### Native Binder RPC experiment

The optional [socket Binder RPC adapter](./binder-rpc.md) uses the image's actual
libbinder implementation and passes local object-ownership tests and two-process
transactions, callbacks, error propagation and remote death. Unavailable services
now have real local Binders that report explicit errors instead of fabricated
successful replies. `network_management` has a typed local interface; its
operations remain unimplemented. This transport does not automatically run GMS,
provide Android caller-UID permissions, or implement kernel Binder.

Play Store's latest 90-second probe completes application/activity initialization
with no native crash or thread-table exhaustion. Fresh frame evidence remains
uniformly white, so visible UI and sign-in remain unverified.
