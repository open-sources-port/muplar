#!/usr/bin/env python3
"""Run the signed-out AccountManager smoke APK in a dedicated rootfs under build/."""
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
    work = ROOT / "build/android-account-test"
    apk = work / "accounts.apk"
    if not apk.is_file():
        parser.error("Build tests/assets/android-accounts/build-apk.sh first")
    socket_name = f"accounts-{os.getpid()}.sock"
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
                        if "ACCOUNT_SERVICE_SMOKE_PASS" in logpath.read_text(errors="replace"):
                            print("AccountManager signed-out queries, listener and async callbacks passed")
                            return 0
                        time.sleep(0.1)
                    raise RuntimeError("Account service smoke failed; see " + str(logpath))
                finally:
                    stop(process)
        finally:
            stop(service)


if __name__ == "__main__":
    raise SystemExit(main())
