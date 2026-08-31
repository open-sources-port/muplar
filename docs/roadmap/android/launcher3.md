# Launcher3 Status

Launcher3 is now the compatibility target for Android home-screen behavior.
This page should stay short: keep detailed implementation plans in focused
documents and use this page to answer, "Can Launcher3 run, and what blocks a
usable user experience?"

## Current State

- Launcher3 starts through guest ART/app_process64.
- `QuickstepLauncher` reaches `onCreate`, `onStart`, `onResume`,
  `makeVisible`, and the main looper.
- The Launcher3 view hierarchy is alive after `makeVisible`:
  `FrameLayout -> LauncherRootView -> DragLayer`, with Workspace, Hotseat,
  ScrimView, and Overview children laid out at real bounds.
- `visual-smoke.sh` can capture a PNG through the Android `Bitmap.compress()`
  path backed by ART-shim pixel storage.
- Instance Manager can list, launch, and install (copy into `packages_dir`
  plus register in Instance Manager's own app list) the packaged Launcher3
  APK and additional APKs, all inside a persistent per-prefix Android
  session with a tabbed device window.
- Java View/HWUI frames present through `HostWindow`, the only host-side
  Android window handler. Launcher3 paints into the device window
  automatically.
- Device-action/input dispatch (Home, Recents, Settings, tab focus/close,
  touch, keyboard) goes through `MuplarSocketClient`, a small native
  `AF_UNIX` socket bridge in the ART shim — the guest connects to muplard's
  Unix socket directly, with no subprocess spawning. This replaced an
  earlier `ProcessBuilder`-spawned-`mup`-subprocess design that could not
  work: guest `execve()` of a host Mach-O binary does not work through
  elfuse (elfuse only loads Linux ELF binaries; confirmed via a kernel-panic
  investigation prompted by trying to force it to). Home/Recents/Settings/
  tab-switching are confirmed working end-to-end through this bridge.
- When Back finishes an app on the Java side, `FrameworkDeviceController`
  reports it to muplard (`tab-finished`/`query-tab-finished`) and the host
  polls after sending Back, auto-closing that tab in the device window.
- `android.view.MotionEvent`'s full native surface (all 44 methods this
  framework build declares, verified via `dexdump` against
  `framework-classes4.jar`) is implemented in the ART shim. Pointer events
  from the device window (`AndroidDeviceShell.mm` -> muplard `DeviceInput`
  opcode -> `FrameworkDeviceController.readInputs()`) reach
  `MotionEvent.obtain(...)` and construct a valid event object successfully.
- Touch input dispatch (2026-08-31): `MuplarContext.createWindowManager` and
  `ArtApkMain.attachWindow` were updated to resolve `WindowManagerImpl` through
  `baseContext` (preventing `Activity.getSystemService(WINDOW_SERVICE)` from
  returning `null` before `mWindowManager` is set). This resolved an NPE during
  `QuickstepLauncher.onCreate` (`UnfoldMoveFromCenterAnimator.<init>`), allowing
  the View hierarchy to fully initialize. Touch dispatch (`ACTION_DOWN` /
  `ACTION_UP`) via `muplard` `DeviceInput` now passes end-to-end through
  `dispatchTouchEvent` and `recycle()` without crashing (`test-touch-smoke.sh`
  passes cleanly).

## User-Visible Problem

The device window session/tab UI, Home/Recents/Settings dispatch, `LauncherApps`
installed-app query, and touch input dispatch all work now. Two primary items
remain to achieve a seamless "Android device" feel: Back button hangs on
apps driving un-ticked animations, and continuous frame rendering needs
optimization under interpreter performance.

The product target is documented in:

- [Android Device Window](./device-window.md)

## Main Blockers

- Back button animation & Choreographer looper hangs: **Resolved (2026-08-31)**. `Java_android_view_DisplayEventReceiver_nativeGetLatestVsyncEventData` now constructs a valid `VsyncEventData` instance with a 7-element `FrameTimeline` array, and `MuplarVsyncScheduler` dispatches `VsyncEventData` overloads cleanly. `FrameworkDeviceController.performBack()` includes a 250ms delayed fallback check to finish activities trapped in transition animations.
- Touch input dispatch: **Resolved (2026-08-31)**. Touch input dispatch now
  executes cleanly through `dispatchTouchEvent` and `recycle()` without native
  crashes. Automated test coverage is added in `test-touch-smoke.sh`.
  would be caught and logged) — this is a hard native crash, so it's very
  likely a missing/broken native dependency somewhere inside real
  Quickstep touch handling (ripple/touch-feedback, `VelocityTracker`,
  `Choreographer`, etc.), not `MotionEvent` itself.
  - Two real bugs were found and fixed on the way here, both still worth
    knowing about:
    1. Calling `MotionEvent.setSource()` after `obtain()` triggered an ART
       interpreter argument-marshaling bug — the follow-up
       `nativeSetSource(J I)V` call received garbage (`ptr`/`source`
       values that looked like leftover stack addresses from the
       *previous* native call's locals), despite the registered JNI
       signature exactly matching the `dexdump`-verified declared
       signature. Fixed by switching to the public 14-arg
       `MotionEvent.obtain(downTime, eventTime, action, pointerCount,
       PointerProperties[], PointerCoords[], metaState, buttonState,
       xPrecision, yPrecision, deviceId, edgeFlags, source, flags)`
       overload, which passes `source` straight into `nativeInitialize`
       and never calls `setSource`/`nativeSetSource` at all.
    2. Enabling ART's JIT (dropping `-Xint`/`-Xusejit:false` in
       `art_bootstrap.cpp`) reproduced a crash with the same signature —
       consistent with hitting the same underlying elfuse/HVF
       instability via JIT code-cache `mmap`/`mprotect(PROT_EXEC)`. This
       was reverted; `-Xint` stays until that's root-caused separately.
       **Caveat:** the touch-dispatch crash above still reproduces with
       `-Xint` restored, so JIT is not the cause of it — it was only
       coincidentally triggered during that experiment.
  - Separately, this build runs `-Xint` (pure interpreter, no JIT) for
    stability, which makes any touch dispatch that *does* succeed very
    slow — a burst of drag events (mouse-drag samples) can queue 100+
    `DeviceInput` frames that take a long time to drain serially on the
    main looper, one Quickstep-interpreted dispatch at a time. The guest
    process can also be observed pegging ~500% CPU continuously without
    crashing while working through a backlog like this — that's a real,
    separate symptom, not necessarily a hang.
  - **Root-cause update (2026-07-29):** a *local, uncommitted, reverted*
    patch to elfuse's SIGSEGV-delivery path
    (`third_party/elfuse/src/syscall/proc.c`, around the
    `signal_deliver_fault(..., LINUX_SIGSEGV, ...)` call) was used for one
    diagnostic session to unconditionally report the faulting PC/address
    plus a `guest_region_find()` lookup for both — this is a vendored
    third-party dependency, so the patch was deliberately not kept or
    committed without the elfuse maintainer's sign-off; reproducing this
    diagnostic again means re-adding the same small patch locally (see the
    session transcript) or, better, proposing it upstream since it's a
    generically useful fix (this diagnostic was previously gated behind
    `verbose`, which is both off by default and, when turned on for a
    test, generated so much syscall-trace volume it rotated the crash
    straight out of the log within a few million lines before the patch —
    a real trap; raise `kMaxLogFileBytes` in
    `apps/macos/PrefixManagerApp.mm`, currently 5MB, before ever trying
    `--verbose` for this again). One captured crash showed *two*
    faults back to back:
    1. The real crash: `PC=0x2013debfc` inside a small (~80KB) **unnamed,
       anonymous, executable** guest region — not a named `.so`, so almost
       certainly ART-generated code (an interpreter trampoline/stub;
       `-Xint` should rule out full JIT-compiled method bodies, though an
       always-on "quick entrypoint" stub cache is plausible) — dereferences
       a garbage 64-bit value (`0x2c22b3a800000000`, not a remotely valid
       address) as a pointer.
    2. A **second, separate** null-pointer crash inside bionic itself
       (`PC=0xff0009a078`, fault address `0x0`, region resolves to the
       sysroot-mapped libc), immediately after step 1's signal handler
       tries and fails to connect to `tombstoned` (which nothing in this
       stack implements). This is bionic's own tombstoned-unreachable
       fallback path null-derefing — a real, independent bug that's
       currently masking any further diagnostics bionic's crash handler
       might otherwise produce.
    Next step to actually root-cause #1: disassemble/symbolicate the
    unnamed region at crash time (no OAT/boot-image symbol source is
    vendored here yet, so this needs either a boot-image symbol dump or
    stepping through with a debugger attached to the vcpu thread) rather
    than more log-based guessing.
- The device window shows a stale/premature frame, not a live one — and
  the real root cause is `-Xint` interpreter performance, not missing
  scheduling. `MuplarFramePresenter.schedule()` originally only captured
  a screenshot when a device action fired (`focusRecord`'s
  `scheduleFrame`, or an input dispatch's own `scheduleFrame` at the end
  of `applyInputOnMain`) — once immediately, plus one retry 48ms later,
  never again. A self-rescheduling 200ms loop was added
  (`MuplarFramePresenter.startLoop`, 2026-07-29) so *something* keeps
  asking for frames, and it does work — confirmed via
  `[Muplar/Window] frame loop started` / `software frame presented` log
  lines, and it did produce one real, correctly-sized frame. But in a
  ~15-20s interactive test it produced **exactly one** frame while 44
  queued touch/input events sat completely unprocessed (zero
  `input apply` lines) the whole time. The main thread is simply too
  backlogged under pure interpretation to get back to a 200ms-interval
  Runnable in any reasonable time — this is the same underlying cause as
  the earlier-observed ~500% sustained CPU with an undrained input
  queue. Scheduling more frame requests can't fix a main thread that
  can't keep up; the loop is correct but can't overcome this on its own.
  **This ties directly back to the reverted JIT experiment**: JIT is
  very likely necessary for this to feel responsive at all, but currently
  crashes via the elfuse/HVF bug documented in the touch-crash section
  above. Getting JIT stable (or finding another way to cut
  interpreted-execution cost) looks like a prerequisite for the device
  window feeling live, not an independent nice-to-have.
  Separately: `visual-smoke.sh`'s independent Bitmap.compress()-based
  capture completes and produces a non-empty image, but **that image is
  itself a checkerboard artifact, not a normal Launcher3 UI**
  (`build/launcher3/verification/all-apps.png`, captured 2026-07-29) — a
  separate, likely pre-existing bug in that capture path worth a look on
  its own, unrelated to interpreter speed.
- Android task/back-stack state is host-simulated (simple tab list), not
  backed by real ActivityManager task tracking.
- The Java View frame path is still the software bitmap bridge
  (`MuplarFramePresenter` writing raw MHR frames); it should move to
  framework/HWUI-backed presentation.

## Verification Commands

```sh
cmake --build build --target mup populate_manager_bundle -j$(sysctl -n hw.ncpu)
platform/android-aarch64/compat/launcher3/smoke-launch.sh
platform/android-aarch64/compat/launcher3/visual-smoke.sh
rg -n "makeVisible|entering main looper|exit code|UnsatisfiedLinkError|No implementation found" \
  "${TMPDIR:-/tmp}/muplar-launcher3-smoke.log"
```

## Keep This Page Clean

Do not add long historical checklists here. If a task needs more than a few
bullets, create or update a focused roadmap document and link it from this
page.
