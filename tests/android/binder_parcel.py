#!/usr/bin/env python3
"""Run the Binder and Parcel interop smoke APK in a dedicated rootfs under build/."""
import argparse
import json
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
    work = ROOT / "build/android-binder-test"
    apk = work / "binder.apk"
    if not apk.is_file():
        parser.error("Build tests/assets/android-binder/build-apk.sh first")
    socket_name = f"binder-{os.getpid()}.sock"
    sock = sysroot / "tmp" / socket_name
    logpath = work / "runtime.log"
    os.chdir(ROOT)
    with (work / "service.log").open("w") as service_log:
        service = subprocess.Popen([
            str(ROOT / "build/bin/muplard"), "--socket", str(sock),
            "--registry", str(work / "packages.properties"),
            "--pid-file", str(work / "service.pid"),
            "--settings", str(work / "settings.db"),
        ], stdout=service_log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 5
            while not sock.exists() and time.monotonic() < deadline:
                if service.poll() is not None:
                    raise RuntimeError("Private service exited; see service.log")
                time.sleep(0.05)
            if not sock.exists():
                raise RuntimeError("Private service did not start; see service.log")
            env = dict(os.environ, MUPLAR_SERVICE_SOCKET="/tmp/" + socket_name)
            with logpath.open("w") as log:
                process = subprocess.Popen([
                    str(ROOT / "build/bin/mup"), "--sysroot", str(sysroot),
                    "--apk", str(apk),
                ], env=env, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 25
                    while process.poll() is None and time.monotonic() < deadline:
                        text = logpath.read_text(errors="replace")
                        if "BINDER_PARCEL_SMOKE_PASS" in text:
                            break
                        if "failed to invoke onCreate" in text or "MUPLAR CRASH HANDLER" in text:
                            break
                        time.sleep(0.1)
                    text = logpath.read_text(errors="replace")
                    phases = [line.split("BINDER_PARCEL_SMOKE_PHASE ", 1)[1]
                              for line in text.splitlines()
                              if "BINDER_PARCEL_SMOKE_PHASE " in line]
                    native_crash = "MUPLAR CRASH HANDLER" in text
                    activity_failed = "failed to invoke onCreate" in text
                    protocol_error = "Unknown binder address" in text or "Record of sending binder" in text
                    passed = ("BINDER_PARCEL_SMOKE_PASS" in text
                              and not protocol_error
                              and not native_crash and not activity_failed)
                    result = {
                        "passed": passed,
                        "protocol_error": protocol_error,
                        "binder_transport": "rpc" if "[Muplar/BinderRPC] native socket transport initialized" in text else "kernel-unavailable",
                        "last_phase": phases[-1] if phases else None,
                        "native_crash": native_crash,
                        "activity_on_create_failed": activity_failed,
                        "binder_driver_unavailable": "Opening '/dev/binder' failed" in text,
                        "process_exit_code_before_stop": process.poll(),
                        "log": str(logpath),
                    }
                    (work / "result.json").write_text(json.dumps(result, indent=2) + "\n")
                    print(json.dumps(result, indent=2))
                    return 0 if passed else 1
                finally:
                    stop(process)
        finally:
            stop(service)


if __name__ == "__main__":
    raise SystemExit(main())
