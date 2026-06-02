# Android Runtime Checklist

Stable area: NativeActivity, JNI/native execution, windowing, input, app loop,
assets, and package context.

## Done

- [x] Direct Android `.so` execution through `JNI_OnLoad`.
- [x] `RegisterNatives` resolution and direct native method invocation.
- [x] NativeActivity `ANativeActivity_onCreate` bootstrap.
- [x] Lifecycle callback dispatch for start, resume, pause, stop, destroy.
- [x] `ANativeWindow` dimensions, buffer lock/unlock, and post path.
- [x] EGL/GLES smoke path through ANGLE-backed host rendering.
- [x] Native app glue pthread scheduling and guest callback invocation.
- [x] `ALooper` add/poll callbacks, input queue attach/detach, and command pipe.
- [x] Host-driven app loop with repeated input/frame scheduling.
- [x] APK launch envelope, asset extraction, `AAssetManager`, package name, and context paths.
- [x] Generic Wine-style prefix metadata with Android ARM64 running through elfuse, keeping prefix policy in Muplar-owned code.
- [x] CLI instance manager for listing, inspecting, cloning, and deleting prefix-backed instances.
- [x] Native macOS instance manager shell for desktop instance management.

## Next

- [ ] Move prefix-aware APK install/run into a long-lived `muplard` session model.
- [ ] Add Muplar-owned writable rootfs routing without adding prefix concepts to elfuse.
- [ ] Add Linux/Wine prefix policies after the Android prefix path is stable.
- [ ] Run less-controlled native APKs with `--host-window` and record first unsupported runtime APIs.
- [ ] Replace conservative libc/file stubs with host-backed behavior only when real APKs need it.
- [ ] Add focused fixtures for new lifecycle or window/input failures before broad refactors.

## Later

- [ ] More complete surface resize, pause/resume, and multi-frame timing behavior.
- [ ] Better rendering capture and visual diff tests for real host-window runs.
