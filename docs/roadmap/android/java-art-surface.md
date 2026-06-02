# Java And ART Surface Checklist

Stable area: Java-facing objects, JNI behavior, Android framework methods, and
the eventual ART-facing compatibility layer.

## Done

- [x] Minimal `JavaVM` and `JNIEnv` tables for `JNI_OnLoad` and native registration.
- [x] Basic JNI class/method/object/string/byte-array helpers used by current fixtures.
- [x] Lightweight NativeActivity context object for package name and code path queries.
- [x] Package metadata and data paths are populated from APK manifest information.
- [x] Java-only APKs are classified before native launch and report missing ART bootstrap inputs.
- [x] ART sysroot checker/import helper records required `app_process64`,
      bootclasspath jar, and native runtime library inputs.
- [x] Ready ART bootstrap plans are wired into `GuestRunner` with guest-visible
      `app_process64` paths, APK staging, and Android environment overrides.
- [x] Muplar Java bootstrap jar provides the `com.muplar.runtime.ArtApkMain`
      entrypoint used by `app_process64`.
- [x] `ArtApkMain` creates an Android APK class loader and resolves the
      manifest launch Activity class when ART reaches Java code.
- [x] Tiny Java Activity APK fixture is classified and carries its launch
      Activity into the ART bootstrap plan.

## Next

- [ ] Import an ART-capable Android sysroot with `tools/import-android-art-sysroot.sh`
      and verify it with `tools/check-android-art-sysroot.sh`.
- [ ] Run the tiny Java Activity fixture through `app_process64` and record
      first Java/framework method gaps. Use `tools/run-tiny-java-activity-art.sh`
      once the sysroot is ready and `d8` is available.
- [ ] Run a simple Java launcher app with enough Activity/framework surface to
      display and launch one installed package.
- [ ] Use Launcher3 as the compatibility target after the simple launcher path
      is stable.
- [ ] Add Java-side API stubs only when tied to a real startup failure or focused fixture.
- [ ] Keep Java/ART expansion separate from native dependency closure work.

## Later

- [ ] More faithful object identity, class hierarchy, method dispatch, and exception behavior.
- [ ] Larger Android framework surface once native execution reaches real app startup reliably.
