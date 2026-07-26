# Android Framework Services Architecture

Muplar will host shared Android framework services in a persistent per-prefix
daemon named `muplard`. Applications remain separate processes and connect to
the daemon through a Muplar Binder transport.

## Process Model

- One `muplard` process owns the service registry for each Android prefix.
- ActivityManager, PackageManager, LauncherApps, settings, shortcuts, and
  widget services are hosted by that daemon or supervised service workers.
- Client crashes must not terminate services or invalidate other clients.
- In-process service adapters are allowed only for bootstrap tests and early
  compatibility fixtures.

## Transport

- Use framed per-prefix Unix `SOCK_STREAM` sockets; macOS does not support
  `SOCK_SEQPACKET` for `AF_UNIX`.
- Carry Binder-style handles and transaction metadata in a versioned protocol.
- Pass file descriptors with `SCM_RIGHTS` for `ParcelFileDescriptor` and shared
  buffers.
- Authenticate clients using peer credentials and prefix-scoped endpoints.
- Define cancellation, death notifications, transaction limits, and daemon
  restart behavior before exposing the transport as a stable ABI.

## Delivery Order

1. Service registry, client identity, and death notifications.
2. PackageManager and package-change broadcasts.
3. ActivityManager lifecycle and task tracking.
4. LauncherApps, ShortcutService, and minimal AppWidgetService stubs.
5. Window/input service integration and permission enforcement.

Implemented transport primitives include correlated request/reply routing,
service-owner death handling, package-generation subscriptions, and verified
`SCM_RIGHTS` descriptor transfer. The NDK Binder service-manager and transaction
adapter now routes daemon-owned services through this transport while preserving
in-process guest services. Java Binder, Parcel, and ServiceManager client adapters
use the same parcel envelope and transaction router.

## Service Host Baseline

`muplard` publishes lifecycle readiness, restart generation, uptime, and a
catalog of Muplar-owned framework substitutes. Prefix-persistent policy state
tracks users and package permission overrides. Unknown profiles are rejected,
dangerous permissions require an explicit grant, and normal permissions use a
default grant for known users.

The surface service provides atomic create/remove, visibility, layer, alpha,
position, and crop transactions. Java `SurfaceControl` clients use that state
model while Muplar's existing host-window paths remain responsible for pixel
presentation. This is the Launcher3 startup baseline, not complete AOSP
SurfaceFlinger, SELinux, or Android runtime-permission parity.

## Framework Hardening & Diagnostic Stubbing

Plans for handling framework fallback logs and diagnostic sockets in containerized desktop environments:

### 1. Synthetic Wallpaper Service (`WallpaperManager`)
- **Background:** `SemWallpaperResourcesInfo` / `WallpaperManager` logs `Resources$NotFoundException: Resource ID #0x0` when attempting to load OEM device wallpapers in a headless sysroot.
- **Plan:** Provide a synthetic `WallpaperManagerService` in `MuplarServices` that returns a default desktop background bitmap without raising `Resources$NotFoundException`.
- **Impact:** 0% impact on standard Android apps/games. Eliminates log noise for custom Android launchers.

### 2. Feature Flag Compatibility (`SemFloatingFeature`)
- **Background:** Android framework queries hardware capabilities (S-Pen, display refresh rate, fold state) using legacy fallback tags.
- **Plan:** Keep `IPackageManager` and `Build.VERSION` system feature responses aligned with standard AOSP API-35 specifications.
- **Impact:** 0% impact on apps.

### 3. Diagnostic Socket Stubbing (`tombstoned`)
- **Background:** Guest Bionic libc outputs `libc: failed to connect to tombstoned` when crash/diagnostic sockets (`/dev/socket/tombstoned`) are absent.
- **Plan:** Stub `/dev/socket/tombstoned` in `muplard` or filter guest Bionic socket warnings so guest logs remain clean while host macOS debugging (`lldb`, `mup` logs) continues to capture native crashes directly.
- **Impact:** Purely log hygiene; no operational impact on app execution.

