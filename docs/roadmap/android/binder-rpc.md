# Binder transport decision and experimental socket backend

Decision, November 25, 2026: retain Muplar's ART userland and use the **real
platform libbinder socket RPC transport** for explicit remote endpoints. Do not
emulate a successful kernel driver with a placeholder `/dev/binder`, suppress
Binder objects into null values, or report successful unimplemented transactions.
This is an experimental backend, not full Android Binder compatibility.

## Why this path

The selected Android image already exports `RpcServer` and `RpcSession`. Those
provide actual socket connections, native Binder references, transaction dispatch,
callbacks and peer-death handling. A kernel backend would additionally require a
real Binder driver, an Android service manager and framework service processes;
creating a device file alone supplies none of those.

The stock Java JNI bridge also assumes kernel `IPCThreadState` in its callback,
interface-enforcement and proxy-finalizer paths. The adapter therefore uses the
image's native `Parcel` and `BBinder` implementation with a narrow Java bridge and
an owning RPC proxy. It preserves the framework's native local Binder holder and
finalizer. Genuine framework BinderProxy methods are no longer overwritten by
successful placeholder operations when native Binder registration succeeds.

Local parcels retain native strong references in a per-parcel object table.
A real loopback exchange balances RPC object references before an object is
retained for a local read. A reference acknowledgement barrier prevents idle
connections retaining references indefinitely. Recycling, destruction, truncation
and append operations preserve or release that ownership. Binder-containing local
parcels cannot be marshalled into untracked raw bytes. Remote transactions use
connected RPC sessions and native wire encoding, including re-encoding local
parcel object references for the destination session.

The local relay uses one server worker and no incoming client callback pool.
Explicit remote connections enable incoming workers for callbacks. This limits
pressure on elfuse's current 64-thread table; it does not increase that limit.

Bionic's accept hook queries `SO_DOMAIN`, which the current elfuse socket-option
translation does not implement. The ART preload shim derives this value from the
actual socket address returned by `getsockname`; other options retain their
normal implementation.

## Scope and limits

The adapter is loaded only when `libmuplar_binder_rpc.so` is explicitly staged
beside the ART preload shim. Initialization failure is fatal to bootstrap, rather
than continuing with partially registered JNI methods. Without that artifact,
the existing kernel-dependent path remains available for reproducing its failure.

`MuplarBinderRpc.startServer`, `connect` and `stopServer` provide explicit socket
endpoints. Server handles are checked opaque IDs; native proxy references have
owned lifetimes and weak Java identity caching. Callback exceptions become error
replies. Unknown transaction codes return false. Dead endpoints report
`DeadObjectException`; death registration/unregistration uses native libbinder.

This does **not** implement:

- `/dev/binder`, kernel IPC compatibility or automatic kernel/RPC bridging.
- Transparent discovery/startup of GMS, GSF, authenticators or content providers.
- Android caller-UID credentials, per-package permissions or multi-user policy.
  Endpoints are intended only inside a private test instance. Socket modes alone
  do not supply Android's security model.
- File-descriptor transfer through this adapter, cross-session Binder forwarding,
  frozen-process callbacks or the complete set of native/Java Binder entry points.
- Arbitrary Android-image ABI compatibility. The C++ bridge is built against
  pinned AOSP headers and the selected image's platform libraries; changes to
  either require rebuilding and repeating the interoperability tests.

`UnavailableServiceBinder` replaces `NoNativeBinder` with a real local Binder.
It returns an explicit unsupported-operation error for synchronous calls and does
not report successful one-way execution. `network_management` has a local typed
interface whose unimplemented operations raise explicit errors. No network daemon,
quota manager or firewall behavior is invented.

A full Android system backend remains an alternative if broad service/security
compatibility is required. The RPC result establishes that a genuine transport
can run in this userland; it does not establish that the remaining Android system
services can be replaced cheaply or faithfully.

## Reproduction

Use a dedicated rootfs under `build/`, as described in [the Play Store plan](./play-store.md).
The header download is pinned to immutable AOSP commits and content hashes;
subsequent builds verify the cached header inventory. Downloaded headers and
binaries remain build artifacts.

```sh
sh tools/build-art-bootstrap-jar.sh --no-sync
zsh tools/build-android-art-shim.sh --no-sync
python3 tools/build-android-binder-rpc.py \
  --sysroot build/opengapps/probe-rootfs --fetch-headers \
  --output build/opengapps/probe-rootfs/data/local/tmp/muplar/art/libmuplar_binder_rpc.so
```

Stage the newly built bootstrap jar and preload shim into the dedicated rootfs
and the local `build/builtin-android/` runtime artifact directory as needed by
`mup`; do not sync them to a user's running prefix. Build and run both tests:

```sh
bash tests/assets/android-binder/build-apk.sh
bash tests/assets/android-binder-rpc/build-apk.sh
python3 tests/android/binder_parcel.py --sysroot build/opengapps/probe-rootfs
python3 tests/android/binder_rpc.py --sysroot build/opengapps/probe-rootfs
python3 platform/android-aarch64/compat/opengapps/probe-launch.py \
  --sysroot build/opengapps/probe-rootfs --timeout 90 --debug-views --fresh-data
```

The local fixture covers Binder identity, nulls, local callback dispatch, error
replies from unimplemented services, append/truncation, descriptor enforcement,
marshalling rejection, strong retention until recycle, eventual weak-reference
collection and repeated allocation/GC.

The two-process fixture checks actual remote results, callbacks into the client,
stable remote proxy identity, exception replies, unknown-transaction rejection,
death unlink, delivered peer-death notification and rejected post-death calls.
Its runner terminates only the server process it started to exercise death.

## Latest Play Store result

With RPC enabled, fresh Play Store app data and managed-module queries returning an empty registry,
application/activity creation and a 90-second main-looper observation pass with
no native crash and no thread-table exhaustion. A fresh 1080×1920 captured frame
is still uniformly white. The debug view tree contains a laid-out application
shell with no visible content. GMS compliance queries time out, and running GMS
services, authentication, catalog access and installation remain unverified.

The probe now records the selected transport, thread exhaustion and fresh frame
evidence, including a PNG and whether the frame is uniform. Startup completion
alone never sets `play_store_support_verified` to true. Continue with actual
GMS/GSF service/provider startup and dispatch; a readable store/sign-in screen
remains an open acceptance criterion.

A separate repeated-run probe entered Play Store's recovery flow and failed in
`njk.onStart()` with a null application field. Its evidence is preserved as
`build/opengapps/launch-recovery.log` and `probe-result-recovery.json`. Successful
fresh-data startup therefore does not establish restart/recovery compatibility.
`--fresh-data` preserves the old Play Store data directory under a timestamped
backup name inside the dedicated rootfs; it does not erase a user's instance.

The local/remote Binder tests, package discovery/signers/modules, manifest,
signed-out accounts, subscriptions, all notification CRUD/restart cases and an
isolated Launcher startup/frame check pass. Both native builds, full CI
clang-format/cppcheck commands, relevant shell syntax checks and `git diff --check`
pass. Only generated build artifacts and the dedicated rootfs were staged.
