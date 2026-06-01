# Java And ART Surface Checklist

Stable area: Java-facing objects, JNI behavior, Android framework methods, and
the eventual ART-facing compatibility layer.

## Done

- [x] Minimal `JavaVM` and `JNIEnv` tables for `JNI_OnLoad` and native registration.
- [x] Basic JNI class/method/object/string/byte-array helpers used by current fixtures.
- [x] Lightweight NativeActivity context object for package name and code path queries.
- [x] Package metadata and data paths are populated from APK manifest information.

## Next

- [ ] Run real APK startup paths and record first Java/framework method gaps.
- [ ] Add Java-side API stubs only when tied to a real startup failure or focused fixture.
- [ ] Keep Java/ART expansion separate from native dependency closure work.

## Later

- [ ] More faithful object identity, class hierarchy, method dispatch, and exception behavior.
- [ ] Larger Android framework surface once native execution reaches real app startup reliably.
