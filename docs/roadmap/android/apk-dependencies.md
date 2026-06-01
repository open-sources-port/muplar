# APK Dependencies Checklist

Stable area: APK extraction, native library selection, `DT_NEEDED` closure,
direct relocation diagnostics, and strict import behavior.

## Done

- [x] Extract APK `lib/arm64-v8a` native libraries.
- [x] Select NativeActivity library from manifest `android.app.lib_name`.
- [x] Map APK-local direct dependencies into guest memory.
- [x] Resolve transitive APK-local dependency chains.
- [x] Apply direct `.so` RELR and RELA/PLT relocations in dependency order.
- [x] Fail early when a required non-builtin `DT_NEEDED` library is missing.
- [x] Resolve known Android/runtime symbols through builtin stub tables.
- [x] Patch unresolved strong direct imports to diagnostic trap stubs in exploratory mode.
- [x] Add `--strict-direct-imports` for prelaunch compatibility checks.
- [x] Keep Android HVC stubs in a larger dedicated arena so they do not overwrite JNI tables.
- [x] Clear the generated NDK AIDL fixture under strict native import scanning.

## Next

- [ ] Run compatibility scans against real APK batches and use reports as the stub backlog.
- [ ] Promote repeated missing imports into focused runtime stubs with fixtures.
- [ ] Keep intentionally unsupported imports visible through strict scan failures.

## Later

- [ ] Support additional APK packaging layouts only when encountered in real apps.
- [ ] Add host-backed file/system behavior for conservative libc stubs when needed.
