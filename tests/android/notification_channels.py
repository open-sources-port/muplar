#!/usr/bin/env python3
"""Test channel metadata, package isolation and cold-restart persistence in a private rootfs."""
import argparse
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sysroot", required=True, type=Path)
    args = parser.parse_args()
    sysroot = args.sysroot.resolve()
    if ROOT / "build" not in sysroot.parents:
        parser.error("Use a dedicated rootfs copy under build/")
    work = ROOT / "build/android-notification-test"
    for name in ("first", "second"):
        if not (work / (name + ".apk")).is_file():
            parser.error("Run tests/assets/android-notifications/build-apk.sh first")
        pkg = "com.muplar.notificationsmoke." + name
        base = sysroot / "data/system/muplar/notification-channels/u0" / (pkg + ".bin")
        for suffix in ("", ".new", ".bak"):
            Path(str(base) + suffix).unlink(missing_ok=True)
        (sysroot / "data/user/0" / pkg / "files/persisted-channel").unlink(missing_ok=True)
    sockname = f"channels-{os.getpid()}.sock"
    sock = sysroot / "tmp" / sockname
    os.chdir(ROOT)
    with (work / "service.log").open("w") as service_log:
        server = subprocess.Popen([
            str(ROOT / "build/bin/muplard"), "--socket", str(sock),
            "--registry", str(work / "packages.properties"),
            "--pid-file", str(work / "service.pid"), "--settings", str(work / "settings.db"),
        ], stdout=service_log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 5
            while not sock.exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            if not sock.exists():
                raise RuntimeError("Private service failed to start")
            for name, phase in (("first", "create"), ("second", "create"),
                                ("first", "persist"), ("second", "persist")):
                logpath = work / f"{name}-{phase}.log"
                with logpath.open("w") as log:
                    process = subprocess.Popen([
                        str(ROOT / "build/bin/mup"), "--sysroot", str(sysroot),
                        "--apk", str(work / (name + ".apk")),
                    ], env=dict(os.environ, MUPLAR_SERVICE_SOCKET="/tmp/" + sockname),
                        stdout=log, stderr=subprocess.STDOUT)
                    try:
                        deadline = time.monotonic() + 30
                        expected = ("NOTIFICATION_CHANNELS_SMOKE_PASS phase=" + phase +
                                    " package=com.muplar.notificationsmoke." + name)
                        while time.monotonic() < deadline:
                            text = logpath.read_text(errors="replace")
                            if expected in text:
                                print(name + ": " + phase + " passed", flush=True)
                                break
                            if process.poll() is not None or "failed to invoke onCreate" in text:
                                raise RuntimeError("Channel test failed; see " + str(logpath))
                            time.sleep(0.1)
                        else:
                            raise RuntimeError("Channel test timed out; see " + str(logpath))
                    finally:
                        stop(process)
        finally:
            stop(server)
    print("Notification channel CRUD, defensive copies, package isolation and restart persistence passed")


if __name__ == "__main__":
    main()
