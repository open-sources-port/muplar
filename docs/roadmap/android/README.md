# Android Roadmap

This folder tracks Android work by stable product/runtime areas. Keep the
overview focused and move detailed implementation notes into a dedicated file.

## Current Focus

Build a user-friendly Android device session:

- One persistent Android window per prefix.
- Launcher3 starts automatically as Home.
- Back, Home, and app switching are available from host controls.
- Apps open as tabs/tasks inside the same Android session, not as separate
  macOS windows.

Start here:

- [Android Device Window](./device-window.md)
- [Launcher3 Status](./launcher3.md)

## Supporting Areas

- [Runtime](./runtime.md)
- [Binder and AIDL](./binder-aidl.md)
- [APK Dependencies](./apk-dependencies.md)
- [Compatibility Scanning](./compatibility-scanning.md)
- [Java and ART Surface](./java-art-surface.md)
- [Launcher and Launcher3](./launcher3.md)
- [Framework Services Architecture](./framework-services.md)
- [Android Sysroot](./sysroot.md)

## Maintenance Rule

If a roadmap file becomes hard to scan, split it. Each file should answer one
question clearly:

- What works?
- What is blocking the next useful user-visible result?
- What is the next small set of steps?
