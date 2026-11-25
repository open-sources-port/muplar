#!/usr/bin/env python3
"""Run the installed APK package discovery smoke APK in a dedicated rootfs under build/."""
import argparse
import hashlib
import json
import shutil
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
    work = ROOT / "build/android-package-test"
    apk = work / "packages.apk"
    if not apk.is_file():
        parser.error("Build tests/assets/android-packages/build-apk.sh first")
    fixture = ROOT / "build/opengapps/fixture"
    entries = []
    guest = sysroot / "data/local/tmp/muplar/package-smoke"
    guest.mkdir(parents=True, exist_ok=True)
    for package in json.loads((fixture / "provenance.json").read_text())["packages"]:
        source = fixture / package["file"]
        with source.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if digest != package["sha256"]:
            raise RuntimeError("Fixture integrity failed: " + str(source))
        shutil.copy2(source, guest / source.name)
        entries.append(f"package={package['package']}\nactivity=\napk=/data/local/tmp/muplar/package-smoke/{source.name}\n---\n")
    # Duplicate self registration must not duplicate enumeration; wrong identities are rejected.
    entries.append("package=com.muplar.packagesmoke\napk=/missing.apk\n---\n")
    entries.append("package=com.muplar.wrongidentity\napk=/data/local/tmp/muplar/package-smoke/PlayStore.apk\n---\n")
    (work / "packages.properties").write_text("".join(entries))
    socket_name = f"packages-{os.getpid()}.sock"
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
                    deadline = time.monotonic() + 180
                    while process.poll() is None and time.monotonic() < deadline:
                        text = logpath.read_text(errors="replace")
                        if "failed to invoke onCreate" in text or "MUPLAR CRASH HANDLER" in text:
                            break
                        if "PACKAGE_DISCOVERY_SMOKE_PASS" in text:
                            print("Package discovery and signer checks passed")
                            return 0
                        time.sleep(0.1)
                    raise RuntimeError("Package discovery smoke failed; see " + str(logpath))
                finally:
                    stop(process)
        finally:
            stop(service)


if __name__ == "__main__":
    raise SystemExit(main())
