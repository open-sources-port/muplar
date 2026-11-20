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

### Latest isolated startup result after channel support

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

**Next vertical slice: coherent native Binder/Parcel interoperation.**

Source inspection shows that genuine Parcel JNI registration is enabled while
Binder registration is absent from the framework registration list, and the
shim's `getNativeBBinderHolder()` returns a synthetic shared token. The abort
stack returns to `ibinderForJavaObject+0x50`; disassembly places that call site
immediately after JNI `IsInstanceOf`. Uninitialized Binder JNI class metadata
and synthetic-holder interoperability are therefore concrete investigation
targets, not yet a verified root-cause fix.

1. Add a minimal real-ART reproducer using `new Binder()`,
   `Parcel.writeStrongBinder()` / `readStrongBinder()`, identity and repeated
   allocation/GC. Capture the original JNI failure before the fatal unwind.
2. Establish consistent framework Binder JNI registration and valid local native
   holder ownership/finalizers. Retain the required emulated remote transport.
   Simply initializing offsets while leaving an incompatible fake holder is
   insufficient.
3. Test callback Binder objects and death/lifecycle behavior through the bridge,
   then rerun the same Play Store fixture and record its next failure.
4. Do not suppress the crash by silently serializing null binders or fabricating
   successful transactions. Keep notification/account/Launcher regressions passing.

The generated report records application/activity failures, whether the main
looper was entered, native crashes, pre-stop process status, startup timeouts, and unsupported
system-service names.
`play_store_support_verified` remains false: this probe cannot certify sign-in,
catalog access, or installation.

### Static implementation gaps

| Area | Current implementation | Work required |
| --- | --- | --- |
| Account manager | Framework frontend and signed-out query/listener bridge implemented; authentication returns explicit unsupported errors | Authenticator execution, account persistence, identity/permission enforcement and sign-in |
| Package discovery | `getInstalledPackages()` returns an empty list; package-info lookup is largely self-only | Cross-package identity, versions, signatures, components, metadata and updates |
| Signature checks | `checkSignatures()` currently returns `SIGNATURE_MATCH` unconditionally | Actual signer comparison and correct mismatch/unknown-package results |
| Services | General `startService()` returns null; binding mostly special-cases Launcher3 | Instantiate declared services, dispatch lifecycle and bind callbacks, retain connections and handle death |
| Service/provider resolution | Service queries are empty and provider resolution is incomplete | Manifest-backed service/provider registries and cross-package loading |
| Native Binder/Parcel | Genuine Parcel JNI coexists with synthetic Binder holders; extended probe aborts in Binder/JNI | Reproduce and resolve registration/holder/lifetime interoperability, then validate real callback round trips |
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
- [ ] Resolve the native Binder/JNI abort before declaring initialization successful.
- [ ] Verify package versions and application metadata needed by initialization.
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
