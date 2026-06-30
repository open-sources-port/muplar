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
