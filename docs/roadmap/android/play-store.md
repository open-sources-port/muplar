# Google Play Store compatibility plan

Status: **experimental; Play Store does not start successfully or support sign-in yet.**

The active application target is Google Play Store from the Open GApps project.
Launcher3 remains the home-screen and input/rendering regression baseline.
This document records the investigation on 2026-11-20 and the implementation
work needed to progress from an APK fixture to a usable store.

## 1. Verified baseline

Muplar currently runs an Android 15 / API 35 ARM64 ART userland through HVF and
elfuse. It supplies framework services through bootstrap Java classes, native
shims, and `muplard`; it does not boot a complete Android system with all the
services expected by Google applications.

The tested fixture contains:

| Component | Package | Tested version code | Source |
| --- | --- | --- | --- |
| Play Store | `com.android.vending` | `83032110` | Original APK in the Open GApps ARM64 source repository |
| Google Play services | `com.google.android.gms` | `214857039` | ARM64, 480-density APK from the Android 11 pico bundle |
| Google Services Framework | `com.google.android.gsf` | `30` | APK from the Android 11 pico bundle |

These are pinned compatibility fixtures, not a claim of support for the latest
Google applications. The Open GApps ARM64 catalog inspected during this work
provided builds through Android 11, not Android 15. Use its bundle as a source
of test artifacts; installing that complete bundle into the API 35 rootfs is
not an implemented or validated provisioning path.

### Artifact integrity

The pinned bundle is `open_gapps-arm64-11.0-pico-20220215.zip`:

```text
SHA256 722bf4395edc62c12ca521d71bb929157b714c240f2557a61d9a30969230ed36
```

Its packaged `Phonesky.apk` fails Android SDK signature verification because
its v2/v3 signatures were stripped during repackaging. Open GApps documents
this behavior and recommends using the original repository APK for ordinary
installation. See [Open GApps issue 913](https://github.com/opengapps/opengapps/issues/913).

The fixture preparation tool instead downloads the original `83032110.apk`
from the Open GApps source repository and checks its Git LFS SHA256:

```text
SHA256 22e9e27d52883cfdfdcf9c32cd1a9676bda41e4060e00295d49aa870be0649e1
```

All three prepared APKs passed `apksigner verify`. Their package identities,
version codes, individual hashes, verification output, and provenance are
written under `build/opengapps/fixture/`. APK binaries are not checked into Git.
Do not re-sign the APKs or bypass signature verification to make a probe pass.

### Completed first fix

Play Store exposes its launcher through an `activity-alias`. The APK manifest
parser now resolves the alias's `targetActivity`, rather than treating the
alias as a Java class or falling back to an unrelated activity.

The real Play Store binary manifest resolves to:

```text
com.google.android.finsky.activities.MainActivity
```

`tests/android/apk_activity_alias.cpp` covers plain XML aliases, a missing
alias target, and optional validation against the real binary APK manifest.
This fix establishes the correct launch target; it does not establish that
Play Store initialization succeeds.

## 2. Confirmed failures and known gaps

### Latest isolated startup result

The initial signature-verified probe failed because the `account` system
service was missing, followed by a null `Point` during display-size discovery.
The next implementation step has now addressed both dependencies:

- `MuplarContext` constructs the framework `AccountManager` with an in-process
  `IAccountManager` bridge. Signed-out queries return empty account/authenticator
  collections; account mutations do not report false success.
- Initial account-listener delivery, feature queries and explicit unsupported
  authentication callbacks have been exercised through the actual Android
  framework frontend. This does not provide authenticator execution, credentials,
  persistent accounts or Google sign-in.
- `DisplayManager.getStableDisplaySize()` now returns a fresh `Point` containing
  the configured display dimensions. The failing Google call was identified
  from the fixture's bytecode. Portrait and landscape smoke tests verify the
  dimensions and that callers cannot mutate shared geometry.

### Completed notification-channel step

The framework-facing `NotificationManager` now has channel metadata backed by
`MuplarNotificationChannels`:

- Channel and group create/query/delete operations return real objects and valid
  `ParceledListSlice` containers, including when the result is empty.
- Duplicate channel creation updates its name/description without raising its
  importance or replacing its established sound. Deletion/recreation retains
  the channel's previous behavior, and group deletion removes its live channels.
- Returned/input objects do not alias stored state. Each package has a separate
  namespace; self-package queries are enforced. This is not a replacement for
  the caller-UID/permission model still required in Gate C.
- Metadata is stored under the selected rootfs at
  `/data/system/muplar/notification-channels/u0/`. Package file locks, fsynced
  sibling temporary files and same-filesystem rename protect updates. Cold-guest
  restart tests verify persistence.
- The storage code avoids Samsung `AtomicFile` hooks: the test rootfs's
  `AtomicFile.startWrite()` invokes an unavailable `ReliableWrite` vendor native.

The implementation currently supports user 0 and public self-package metadata
operations. Posting notifications to macOS, delegation, full user-policy controls,
and migration of serialized metadata across framework/SDK upgrades remain
unimplemented or unverified. Do not mark all NotificationManager behavior complete.

### Historical isolated startup result after channel support

The account service, stable display size, and previous
`getNotificationChannels()` null-list failure are no longer the observed startup
exceptions. The 25-second observation did not reach the main looper; a longer
90-second observation reproduced a native Binder/JNI abort:

- `entered_main_looper` remains false and `play_store_support_verified` remains false.
- Neither application nor activity `onCreate` exception markers appeared in that
  extended run. That does **not** prove either initialization completed.
- Guest logs show `SIGABRT`, with `android::ibinderForJavaObject` in the native
  stack. The host process also exited abnormally. The probe now distinguishes
  a still-running startup timeout from an explicit exception or native crash.
- The remaining unsupported service diagnostic in that run was the Samsung
  `SemPersonaManager`; the earlier `restrictions` request is still unimplemented
  and needs triage when its path is reached.

### Binder registration and isolated reproducer — November 21, 2026

Framework Binder JNI registration is now enabled before Parcel registration.
When it succeeds, the shim retains the genuine local Binder holder allocator
and Binder/BinderProxy finalizers instead of substituting synthetic tokens.
This is partial interoperability work, not a working Binder transport.

The real-ART smoke APK now includes every generated class, including its anonymous
DeathRecipient. It tests local Binder identity, null round trips, a local callback
transaction, local death-registration behavior and repeated allocation/GC. Remote
callback delivery, remote death notifications and finalizer completion are not
established by these tests. Per-operation markers and a machine-readable report
make failures reproducible:

```sh
bash tests/assets/android-binder/build-apk.sh
python3 tests/android/binder_parcel.py --sysroot build/opengapps/probe-rootfs
```

The current expected result is **failure**, not a passing compatibility gate.
`build/android-binder-test/result.json` records `last_phase=roundtrip_write`,
`native_crash=true` and `binder_driver_unavailable=true`. A genuine Binder object
is constructed, but the first `Parcel.writeStrongBinder()` enters
`IPCThreadState::self()` and aborts because `/dev/binder` is absent.

Before the backup/manifest fixes below, a 90-second Play Store probe with this
shim also failed before the main looper. Its log reports that `MuplarServices$NoNativeBinder` is not a native Binder
object, followed by the missing-driver abort from `Parcel.writeInterfaceToken()`.
Neither application nor activity completion was verified in that run. Initializing Binder JNI
metadata therefore exposes two remaining requirements:

1. Provide a coherent native Binder transport for this userland architecture,
   including driver initialization, thread state, reference lifetime and actual
   transaction/error semantics. A placeholder device or successful no-op ioctl
   must not stand in for working transport.
2. Bridge service objects represented by `NoNativeBinder` into valid native
   Binder objects while preserving local interface dispatch and emulated remote
   transport. Keep native holder ownership and finalizers consistent.
3. Pass the local smoke test, then add actual cross-process callback and death
   tests before claiming transport compatibility. Rerun account, notification
   and Launcher regressions and the same verified Play Store fixture.

Do not suppress these failures by serializing null binders or fabricating
successful transactions. The new shim was staged only into the isolated probe
rootfs; it was not deployed to running user instances. AccountManager signed-out
queries/listener/async callbacks and the four notification-channel CRUD/package
isolation/restart cases passed again with this shim. Launcher was not rerun in
this iteration.

The generated report records application/activity failures and completion, whether the main
looper was entered or failed, native crashes, pre-stop process status, startup timeouts, and unsupported
system-service names.
`play_store_support_verified` remains false: this probe cannot certify sign-in,
catalog access, or installation.

### Manifest loading and optional backup absence

After the manifest fixes, the isolated Play Store fixture completed both
`Application.onCreate()` and `MainActivity.onCreate()` and entered the main looper.
That intermediate run then exited with a null `SubscriptionManager` when Play Store
registered a subscription listener; the subscription milestone below resolves that failure. Logs also report Google Play services as unavailable.
`play_store_support_verified` remains false. No sign-in, catalog or installation
has been verified.

Completed in this iteration:

- The unimplemented `backup` service returns absent instead of a fabricated
  `NoNativeBinder`. Android's real BackupManager safely handles missing backup;
  explicitly registered service binders are still honored. This avoids the
  observed optional-backup path into the unavailable native driver. It does not
  implement native Binder transport or make the Binder/Parcel smoke test pass.
- `MuplarManifestInfo` reads the compiled APK manifest. Application metadata now
  preserves typed values, resolves `android:value` resource references, retains
  `android:resource` IDs, and excludes component metadata from the application
  bundle. No Google-specific metadata value is invented.
- Application themes come from the manifest; activity themes override them,
  with application-theme inheritance and target-theme lookup for aliases.
  Both initial and subsequent activity launches receive their component theme.
  The prior name-based fallback remains for APKs with no declared theme.
- APK class loaders load the declared `org.apache.http.legacy` library from the
  Android image before APK classes. The registry currently supports only this
  library; unknown optional libraries are skipped and missing required libraries
  fail explicitly. This fixes the observed missing `ProtocolVersion` class.
- The probe records application/activity completion separately and rejects a
  `main looper exited:` failure even if the looper was entered earlier.

Real-ART integration checks passed for typed manifest metadata, activity-theme
override, a real legacy HTTP object, missing optional library handling, missing
backup queries/dataChanged, signed-out account callbacks, and notification
channel CRUD/package isolation/restart persistence. Launcher was not rerun.

```sh
bash tests/assets/android-backup/build-apk.sh
python3 tests/android/backup_unavailable.py --sysroot build/opengapps/probe-rootfs
bash tests/assets/android-manifest/build-apk.sh
python3 tests/android/manifest_info.py --sysroot build/opengapps/probe-rootfs
python3 platform/android-aarch64/compat/opengapps/probe-launch.py \
  --sysroot build/opengapps/probe-rootfs --timeout 90
```

The subscription milestone below completes the frontend/listener step and a
90-second live-looper observation. Visible rendering, cross-package GMS discovery
and native Binder transport remain separate acceptance criteria.

Build artifacts were staged under `build/sysroot` and the dedicated probe rootfs
only. Running user prefixes and the application bundle were not updated.

### Latest milestone — no-SIM subscriptions

`MuplarContext` now provides genuine framework `SubscriptionManager` and
`TelephonyRegistryManager` instances through string and typed service lookup.
The bootstrap installs `TelephonyServiceManager` through the framework initializer
when absent, allowing the frontend to resolve local `ISub` and
`ITelephonyRegistry` interfaces through the existing service bridge.

`MuplarSubscriptionService` models an immutable **no-modem/no-SIM** device:

- Subscription lists and active ID arrays are empty, active/capacity counts are
  zero, default/slot/phone/subscription IDs are invalid, and individual subscription
  lookups return absent. No SIM identity, phone number or mobile service is invented.
- Listener registration delivers an initial empty-state notification. The genuine
  framework registry handles executor dispatch, duplicate suppression, removal
  and re-registration. The local service retains no extra listener references
  because its subscription state never changes.
- Unsupported subscription mutations and unsupported registry operations raise
  explicit errors. SIM provisioning, live subscription changes, modem events,
  telephony permissions, multiple users and cross-process Binder are not implemented.

The real-ART test verifies string/typed lookup identity, empty queries and invalid
IDs, three initial/re-registration callbacks, duplicate suppression, removal,
legacy main-looper callback dispatch, and explicit rejection of a default-data
subscription mutation:

```sh
bash tests/assets/android-subscriptions/build-apk.sh
python3 tests/android/subscription_service.py --sysroot build/opengapps/probe-rootfs
```

A fresh **90-second** Play Store probe completed application and activity creation,
entered the main looper and remained running until the harness stopped it. The
report records `main_looper_failed=false`, `native_crash=false`, both `onCreate`
completion flags true, and a null pre-stop exit code. The prior subscription
exception is absent, and Play Store logs its subscription callback.

**The UI is still blank.** The software presenter produced a 1080×1920 frame;
inspection of that frame showed white content. A live looper does not establish
that the store UI or sign-in works. Logs repeatedly report Google Play services
as missing. `play_store_support_verified` remains false; Gate B's visible-screen
acceptance criterion remains open.

Next work is to trace cross-package GMS/GSF discovery, actual package metadata,
versions/signatures and service binding, and diagnose the blank rendered screen.
Installed fixture APKs alone do not establish running Play services. Native
Binder transport remains a separate unresolved compatibility requirement; the
local Binder/Parcel smoke test is still expected to fail.

The final subscription test (including mutation rejection), manifest/shared-library,
backup, signed-out account and all four notification CRUD/restart cases passed.
Launcher and interactive sign-in were not rerun.

The bootstrap was built only under `build/sysroot` and exercised in the dedicated
probe rootfs. Running user instances and the application bundle were not deployed.

### Manifest-backed package discovery — November 25, 2026

`MuplarPackageManager` now resolves self and registered cross-package APKs through
Android's real `getPackageArchiveInfo()` parser. Package/application queries expose
manifest versions, SDK levels, metadata and requested components. Enumeration is
consistent across these two Java APIs and deduplicates self registration. Missing
files and registry names that disagree with the APK identity are rejected.

Signer queries use the framework verifier and return actual `Signature` and
`SigningInfo` objects. The shim now registers the genuine IncrementalManager and
StrictJarFile JNI methods required by the verifier. Package-name signature checks
compare certificate sets instead of always returning a match. Unknown/unparseable
packages return `SIGNATURE_UNKNOWN_PACKAGE`; UID-based checks also return unknown
until stable package UID assignment exists. These checks do not implement a
cross-package permission model, certificate-rotation authorization or system-app
privileges. Unsigned archives remain discoverable without signer flags, but a
signer request for an unverifiable archive fails closed.

A bounded cache stores serialized snapshots keyed by APK path, size, modification
time and parse flags. Returned metadata/certificates cannot mutate the cached
record; signer queries cannot reuse an unverified snapshot. Installed APK files
must be replaced through a managed installation path; this cache is not a defense
against concurrent arbitrary file modification. Split-package installation,
long-flag features beyond the legacy flag mask, Binder-facing package queries,
service/provider execution and package visibility enforcement remain open.

The real-ART package test passes for the three pinned Google APKs: exact version
codes, legacy and modern signer information, Google certificate equality, a
differently signed test APK mismatch, unknown/mismatched package rejection,
version constraints, enumeration consistency and defensive metadata/certificate
copies. The test verifies fixture SHA256 hashes before staging them:

```sh
bash tests/assets/android-packages/build-apk.sh
python3 tests/android/package_discovery.py --sysroot build/opengapps/probe-rootfs
```

**The latest Play Store probe fails earlier than the historical blank-screen
run.** Correct GMS discovery exposes another native Binder path: the log requests
`network_management`, then reports `NoNativeBinder` conversion failure and aborts
in `Parcel.writeInterfaceToken()` / `IPCThreadState::self()` because `/dev/binder`
is absent. Neither application/activity completion nor main-looper entry is
verified with this change. Missing-GMS and null-self-signature diagnostics from
the previous run are absent. This does not establish that GMS is running, and no
new screen or sign-in is verified. `play_store_support_verified` remains false.
The probe now excludes native crashes from its startup-timeout classification.

The next step is a coherent Binder/service substrate, beginning with the existing
Binder/Parcel reproducer and the `network_management` request. Reverting truthful
package discovery or inventing successful remote transactions would conceal this
dependency. Keep the previous blank-screen milestone as historical evidence only.

Manifest, signed-out account, no-SIM subscription and all four notification
CRUD/restart regressions pass with these changes. An isolated Launcher3 smoke
also completes application/activity creation, enters its main looper and presents
a 1080×1920 software frame; drawer gestures and app-launch interaction were not
rerun. Full CI clang-format and cppcheck commands, shellcheck for changed POSIX
shell scripts, zsh syntax validation and `git diff --check` pass.
Bootstrap and shim builders
accept `--no-sync` to build without updating user installations; use it for
isolated compatibility work. Only build artifacts and the dedicated probe rootfs
were staged in this iteration.

### Native socket Binder RPC milestone — November 25, 2026

The experimental ART backend now uses the selected Android image's real
`libbinder` RPC transport when its optional adapter is staged. The local
Binder/Parcel fixture passes, including native service objects, truthful
unsupported-operation replies, append/truncation, interface enforcement,
marshalling rejection and verified release of strong references after recycling.
A two-guest fixture also passes real socket transactions, remote callbacks,
remote proxy identity, exception replies, unknown-transaction rejection,
unregistration and actual peer-death notification. Calls after peer death fail.
There is no fake `/dev/binder` device or successful no-op transaction.

`NoNativeBinder` has been replaced by genuine local Binder objects. The
`network_management` interface is locally reachable and its unsupported
operations explicitly raise errors. No network-management functionality is
claimed. Framework Binder/BinderProxy methods and native holder ownership are
preserved when framework JNI registration succeeds. Socket RPC uses an owning
proxy adapter to avoid the stock JNI bridge's kernel-only callback/finalizer
paths. The decision, supported subset, limitations and reproduction commands are
recorded in [the Binder RPC architecture decision](./binder-rpc.md).

The next exposed application failure was `getInstalledModules()`. This userland
has no managed-module metadata provider, so package-manager module enumeration
now returns an empty list and unknown module lookup raises `NameNotFoundException`.
No APEX/module package or module-update success is invented.

**Latest 90-second fresh-data observation:** both application/activity creation complete,
the main looper remains running, and no native crash or thread-table exhaustion
is observed. The initial relay configuration consumed extra threads and hit
elfuse's 64-thread table; reducing the local relay's worker configuration avoids
that failure without changing the runtime's thread limit.

**The screen is still blank.** The probe now saves a fresh 1080×1920 frame and
PNG and records `frame.uniform=true`. Optional view tracing shows a laid-out shell
with no visible content; GMS compliance queries fail. A live RPC transport does
not start GMS or GSF, execute authenticators, or supply service/provider discovery.
No readable store/sign-in UI, sign-in, catalog or installation is verified.
`play_store_support_verified` remains false. Continue with actual GMS/GSF runtime
service/provider dispatch, retaining visible-screen acceptance as an open gate.

The repeated-run recovery probe separately failed during `njk.onStart()` after
Play Store selected `RecoveryModeActivity`. Its log/report are retained alongside
the fresh-data result. The probe's `--fresh-data` option backs up only Play Store's
app data inside the dedicated rootfs; it does not establish persistence/recovery
compatibility. The Binder, two-process RPC, package, manifest, account,
subscription, notification persistence and Launcher startup/frame regressions
pass; full formatting and cppcheck checks pass as well.

### Static implementation gaps

| Area | Current implementation | Work required |
| --- | --- | --- |
| Account manager | Framework frontend and signed-out query/listener bridge implemented; authentication returns explicit unsupported errors | Authenticator execution, account persistence, identity/permission enforcement and sign-in |
| Package discovery | Java package/application lookup and enumeration use real APK manifests and requested signer data | Binder-facing consistency, stable identities, visibility policy, splits and update lifecycle |
| Signature checks | Package-name checks compare real certificate sets; unknown/unverifiable packages and UID checks fail closed | Stable UID mapping, rotation authorization and permission enforcement |
| Services | General `startService()` returns null; binding mostly special-cases Launcher3 | Instantiate declared services, dispatch lifecycle and bind callbacks, retain connections and handle death |
| Service/provider resolution | Service queries are empty and provider resolution is incomplete | Manifest-backed service/provider registries and cross-package loading |
| Native Binder/Parcel | Optional native socket RPC adapter passes local ownership and two-process call/callback/death tests; latest Play Store run has no Binder abort | Kernel IPC, credentials/permissions, FD transfer, transparent service mapping and broader native/Java entry points |
| Network/background work | Connectivity and job APIs include bootstrap stubs | Real behavior for the methods exercised by Play Store and GMS |
| Notification channels | Persistent package-scoped channel/group metadata and public CRUD/restart tests implemented | Notification delivery, full policy/permissions and framework-format migrations |
| Downloads and installation | Full Play Store download-to-install flow is unverified | Persistent downloads, installer sessions, split APKs, update/rollback behavior and truthful status callbacks |

Code entry points:

- [APK manifest classification](../../../platform/android-aarch64/apk/apk_envelope.cpp)
- [Notification-channel metadata service](../../../platform/android-aarch64/java-bootstrap/com/muplar/runtime/MuplarNotificationChannels.java)
- [Signed-out account-service bridge](../../../platform/android-aarch64/java-bootstrap/com/muplar/runtime/MuplarAccountService.java)
- [Context and manager construction](../../../platform/android-aarch64/java-bootstrap/com/muplar/runtime/MuplarContext.java)
- [Framework service interfaces](../../../platform/android-aarch64/java-bootstrap/com/muplar/runtime/MuplarServices.java)
- [Package-manager implementation](../../../platform/android-aarch64/java-bootstrap/com/muplar/runtime/MuplarPackageManager.java)
- [Package installer bridge](../../../platform/android-aarch64/java-bootstrap/android/content/pm/MuplarPackageInstaller.java)
- [Application/activity bootstrap](../../../platform/android-aarch64/java-bootstrap/com/muplar/runtime/ArtApkMain.java)
- [Daemon and protocol](../../../platform/android-aarch64/services/)

## 3. Implementation order and acceptance gates

This is a dependency-driven plan, not an exhaustive prediction of every API
Google code will exercise. After each change, rerun the same pinned fixture,
record the next failure, and extend the regression tests. A non-null stub or a
successful main-loop entry is not sufficient evidence for a working store.

### Gate A — reproducible fixtures and launch target

- [x] Pin the Open GApps bundle and original Play Store APK by checksum.
- [x] Verify APK signatures and record package identities and provenance.
- [x] Resolve the actual activity behind the launcher alias.
- [x] Provide an isolated startup probe and machine-readable failure report.

Acceptance: preparation is repeatable, modified artifacts are rejected, and
the binary manifest resolves to the intended activity. Keep these tests when
upgrading any of the three APKs.

### Gate B — unblock application initialization

This is the immediate implementation priority.

- [x] Construct a genuine `AccountManager` frontend backed by an
  `IAccountManager` implementation reachable through the existing service bridge.
- [x] Support truthful signed-out queries: no accounts means empty account
  collections, not null values or invented Google accounts.
- [x] Implement signed-out account queries, initial listener delivery, feature
  callbacks and explicit unsupported errors for asynchronous authentication.
  Expand method coverage when additional real callers require it.
- [x] Trace the null `Point` to `getStableDisplaySize()` and return configured
  dimensions as a fresh object; verify portrait and landscape behavior.
- [x] Implement persistent, package-scoped channel/group metadata and validate
  public CRUD, object-copy isolation, deletion/recreation and cold restarts.
- [x] Enable framework Binder registration and add an isolated real-ART Binder/Parcel reproducer.
- [ ] Implement native Binder transport and bridge service objects; the reproducer
  currently aborts on the missing `/dev/binder` driver.
- [x] Resolve the observed native Binder/JNI abort using the optional real socket
  RPC adapter; validate local ownership plus cross-process callbacks and death.
  This does not implement kernel Binder transport.
- [x] Read self-application manifest metadata/themes and the declared legacy HTTP library.
- [x] Implement no-SIM SubscriptionManager frontend/listeners and keep the main
  looper alive for a 90-second isolated observation.
- [ ] Render a stable, readable Play Store or sign-in screen; the captured frame
  remains blank despite the live looper.
- [ ] Verify cross-package versions, metadata and running Google Play services.
- [ ] Classify optional OEM probes separately from required Android services.

Acceptance: `Application.onCreate()` and `MainActivity.onCreate()` complete;
a stable, readable initial store or sign-in screen renders; no initialization
exception is swallowed into a blank fallback window. Account queries and
callbacks also pass focused tests outside Play Store.

### Gate C — coherent package, service and provider runtime

- [ ] Build package records from manifests, including services, providers,
  receivers, permissions, metadata, versions and signing certificates.
- [ ] Make package enumeration and lookup consistent across the Java package
  manager, Binder-facing package service and `muplard` registry.
- [ ] Assign stable per-package identities and preserve per-instance data
  isolation. Enforce caller/permission checks where an API requires them.
- [ ] Replace unconditional signature matches with verified signer results.
- [ ] Resolve explicit and permitted implicit service intents; load the
  target APK's classes and resources with the appropriate application context.
- [ ] Deliver service creation/start/bind/unbind/destruction callbacks, real
  `IBinder` connections, disconnect/death notifications and cancellation.
- [ ] Implement provider acquisition and required query/update/call operations,
  including `ContentResolver` and observer behavior exercised by GSF/GMS.
- [ ] Deliver required broadcasts and package-change notifications.

Acceptance: two fixture applications can discover each other, bind a service,
exchange Binder calls and receive disconnect callbacks. Provider data is
accessible through the expected contract. Unknown packages and mismatched
signers fail correctly. No test succeeds merely because access is always granted.

### Gate D — GSF/GMS initialization and real account authentication

- [ ] Run the required GSF/GMS service and authenticator components through the
  lifecycle implemented in Gate C.
- [ ] Support the account-authenticator exchange, asynchronous responses,
  activity handoff, cancellation and error propagation.
- [ ] Implement the account/token persistence and storage contracts needed by
  those components, including required Keystore/native dependencies discovered
  in the trace. Define credential storage and cross-instance isolation.
- [ ] Verify HTTPS, certificate validation, DNS, time, connectivity changes and
  required browser/WebView authentication surfaces.
- [ ] Validate successful sign-in, rejection, user cancellation, token refresh,
  account removal and persistence across a complete runtime restart.

Acceptance: a user can complete Google's actual sign-in flow and return to
Play Store with a usable account. Tokens and credentials stay out of diagnostic
logs. Missing hardware-backed features or server-side eligibility failures
must be reported accurately, not replaced with fabricated success.

### Gate E — catalog, background work and downloads

- [ ] Load the authenticated catalog, search, app details and install controls.
- [ ] Implement the network, scheduling, alarms, wake locks and notifications
  required by the observed store/GMS paths.
- [ ] Back downloads with real storage, progress, cancellation, retry and
  interruption recovery. Preserve package size/hash and failure information.
- [ ] Verify filesystem capacity, file permissions and package-specific storage.

Acceptance: search works and a free compatible app downloads completely;
disconnecting the network, cancelling and restarting produce correct states
without busy loops, permanently stuck jobs or duplicate downloads.

### Gate F — install, update, uninstall and launch

- [ ] Implement the `PackageInstaller` session operations used by Play Store,
  including writes, commit, abandonment and result callbacks.
- [ ] Support base/split APK sets, ABI selection, native-library extraction,
  package validation, signature continuity and existing app data on updates.
- [ ] Commit installations consistently to disk and the package registry;
  recover from failure without reporting a partial package as installed.
- [ ] Notify Launcher3, running clients and the store of package changes.
- [ ] Support uninstallation and launch of the resulting application.

Acceptance: install a free compatible app from the actual store, launch it,
update it and uninstall it. Repeat after a runtime restart and exercise failed
commit, insufficient-storage and interrupted-download cases.

### Gate G — reliability and new-instance provisioning

- [ ] Verify GSF/GMS startup order, reconnects, background jobs, store/service
  updates and persistence across repeated cold starts.
- [ ] Keep Launcher3, input, responsive display, clipboard, SQLite, message-queue
  sleeping and idle frame-deduplication regressions passing.
- [ ] Measure idle CPU, frame traffic, memory and shutdown behavior with all
  three Google packages present.
- [ ] Only after the manual end-to-end flow passes, add new-instance download
  and provisioning with tested version pins, signature/hash checks, a shared
  download cache, progress, offline/retry behavior and rollback.
- [ ] Keep compatibility validation distinct from permission to redistribute
  Google applications and from Google device certification requirements.

Acceptance: a newly provisioned test instance can repeat Gates D–F without
manual registry edits, package copying or repeated account setup. Background
services remain responsive while idle CPU settles rather than continuously
rendering or polling.

## 4. Reproduce the current investigation

Run from the repository root on an ARM64 Mac with the configured build,
Android NDK, Python 3.11+, Android SDK build-tools, and `tar` with lzip support
(macOS `bsdtar` was used).

**Stop Android sessions before rebuilding runtime artifacts.** The existing
ART build scripts synchronize outputs into known prefix/sysroot locations;
replacing a shim or jar that a running guest maps can invalidate that session.

```sh
cmake --build build --target populate_manager_bundle

python3 platform/android-aarch64/compat/opengapps/prepare-fixture.py \
  --build-tools /opt/android/sdk/build-tools/36.1.0
```

Adjust the build-tools path for the local SDK. Preparation downloads the pinned
artifacts when absent, rejects checksum/signature failures, and writes
`build/opengapps/fixture/provenance.json` plus verification and badging reports.

Create a **dedicated** rootfs copy. Do not point the probe at a real instance.
The probe only accepts rootfs paths underneath this checkout's `build/` directory.
The following setup block deliberately refuses to overwrite an existing copy:

```sh
set -euo pipefail
ANDROID_SOURCE_ROOTFS="$HOME/.muplar/prefixes/android-arm64/rootfs"
PROBE_ROOTFS="$PWD/build/opengapps/probe-rootfs"
test ! -e "$PROBE_ROOTFS"
cp -cR "$ANDROID_SOURCE_ROOTFS" "$PROBE_ROOTFS"
```

Use that copy for repeated probes; create another fresh copy when testing clean
installation or first-run persistence. `cp -cR` uses macOS copy-on-write cloning.
The source rootfs must already contain the API 35 framework and runtime libraries.

```sh
python3 platform/android-aarch64/compat/opengapps/probe-launch.py \
  --sysroot build/opengapps/probe-rootfs
```

A nonzero exit is expected for the currently failing startup. Inspect:

```sh
cat build/opengapps/probe-result.json
# Full application/exception trace:
less build/opengapps/launch.log
# Private daemon output:
less build/opengapps/service.log
```

The probe stages the verified APKs only in the test rootfs and runs its own
`muplard`. Its registry entries make packages available to the test service;
they do not start GMS/GSF services or implement authentication. It terminates
its own processes after the observation window. It does not drive sign-in,
search, purchases, downloads or installation.

The report's `play_store_support_verified` field is deliberately false even if
startup eventually succeeds. Gates D–G require separate end-to-end evidence.
Generated artifacts and logs under `build/` are disposable; the historical
failure above remains recorded here if the build tree is cleaned.

### Regression checks

Run notification-channel tests in the dedicated rootfs:

```sh
bash tests/assets/android-notifications/build-apk.sh
python3 tests/android/notification_channels.py --sysroot build/opengapps/probe-rootfs
```

The runner launches two different test packages, then launches each again in a
fresh guest process. It checks empty queries, channel/group CRUD, importance
rules, defensive copies, package namespaces and persisted state. It resets only
its own test packages' metadata and phase markers inside the test rootfs.
It does not verify notification delivery or cross-package authorization.

To reproduce the longer Play Store observation and distinguish timeout from
native failure:

```sh
python3 platform/android-aarch64/compat/opengapps/probe-launch.py \
  --sysroot build/opengapps/probe-rootfs --timeout 90
```


Run the signed-out account and stable-display checks against the same dedicated
rootfs. The test APK is never installed in the user's normal instance:

```sh
bash tests/assets/android-accounts/build-apk.sh
python3 tests/android/account_service.py --sysroot build/opengapps/probe-rootfs
MUPLAR_ANDROID_DISPLAY_WIDTH=1920 MUPLAR_ANDROID_DISPLAY_HEIGHT=1080 \
  python3 tests/android/account_service.py --sysroot build/opengapps/probe-rootfs
```

The expected marker is `ACCOUNT_SERVICE_SMOKE_PASS`: empty account queries,
an initial listener snapshot, feature-query callbacks, and add-account/auth-token
errors all complete. Stable display size matches the configured display and
is not shared mutable state. This test is a signed-out baseline, not an
authentication or notification-channel test.


```sh
mkdir -p build/tests
c++ -std=c++20 tests/android/apk_activity_alias.cpp -lz \
  -o build/tests/apk_activity_alias
build/tests/apk_activity_alias build/opengapps/fixture/PlayStore.apk

git diff --check
```

Also run the formatting and full cppcheck commands in
[the CI workflow](../../../.github/workflows/ci.yml), rebuild the Java bootstrap,
and run the relevant existing framework, Binder, installer and rendering tests
for each affected subsystem. Add focused service tests before relying on an
obfuscated Google stack trace as the only coverage.

## 5. What counts as Play Store support

Mark support complete only with recorded results for:

1. Correct artifact verification, package identity and service startup.
2. Stable initial UI and actual user sign-in, including cancellation/failure.
3. Authenticated catalog/search and a real free-app download.
4. Installation, application launch, update and uninstall.
5. Persistence across cold starts, recovery from network/process interruption,
   isolated instance data, and acceptable idle CPU/memory behavior.

Google may apply server-side eligibility requirements independently of the
local API implementation. Google documents the distinction between Android
compatibility and access to licensed Google Play software in its
[compatibility FAQ](https://source.android.com/docs/compatibility/compatibility-faq).
Do not promise that passing a local launch probe guarantees server acceptance.

If the next traces require a broad `system_server`, authenticator, provider or
native Binder implementation that the current userland architecture cannot
faithfully host, record an explicit architecture decision: extend the real
service substrate or introduce a full Android system backend. Repeated
hardcoded success responses, signature spoofing, or silently changing the
Android API level are not completion criteria.
