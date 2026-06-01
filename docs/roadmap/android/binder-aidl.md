# Binder And AIDL Checklist

Stable area: Binder service manager, Binder object lifecycle, Parcel payloads,
AIDL-generated code, and NDK Binder compatibility.

## Done

- [x] Basic service-manager lookup and stable remote Binder handles.
- [x] Binder liveness, ping, declared checks, and ref-count probes.
- [x] `AIBinder_prepareTransaction` and `AIBinder_transact` with typed Parcel storage.
- [x] Primitive Parcel reads/writes and OK status reply headers.
- [x] Local Binder classes, userdata, descriptors, and `onTransact` callbacks.
- [x] String allocator callbacks and string/null-string round trips.
- [x] Integer arrays, string arrays, null arrays, and null elements.
- [x] Parcelable callbacks and ParcelFileDescriptor-style FD payloads.
- [x] Binder lifecycle hardening: death recipients, dead remotes, weak refs, extensions, cleanup.
- [x] Real generated AIDL source fixture through checked-in generated C++.
- [x] Generated NDK AIDL fixture through libc++/NDK wrapper dependencies.

## Next

- [ ] Use strict APK compatibility scans to identify real Binder/AIDL import gaps.
- [ ] Add focused fixtures for each missing generated-code behavior before expanding abstractions.

## Later

- [ ] Broader AIDL type coverage driven by real app startup failures.
- [ ] More faithful Binder driver semantics if real APKs depend on them.
