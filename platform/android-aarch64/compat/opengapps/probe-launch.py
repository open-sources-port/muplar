#!/usr/bin/env python3
"""Probe Play Store startup in a dedicated rootfs copied under build/.

Run prepare-fixture.py first, build mup, then pass --sysroot PATH. The probe
stages the three verified APKs, runs a private muplard, and saves launch.log
and probe-result.json under build/opengapps. It never signs into an account.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[4]
WORK = ROOT / "build/opengapps"


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
    parser.add_argument("--timeout", default=25, type=int)
    args = parser.parse_args()
    sysroot = args.sysroot.resolve()
    if ROOT / "build" not in sysroot.parents:
        parser.error("Use a dedicated rootfs copy under build/, not a running instance")
    if not (sysroot / "system/framework/framework.jar").is_file():
        parser.error("Android framework.jar missing from the test rootfs")
    os.chdir(ROOT)
    fixture = WORK / "fixture"
    metadata = json.loads((fixture / "provenance.json").read_text())
    guest = sysroot / "data/local/tmp/muplar/gapps"
    guest.mkdir(parents=True, exist_ok=True)
    entries = []
    for package in metadata["packages"]:
        source = fixture / package["file"]
        with source.open("rb") as stream:
            actual = hashlib.file_digest(stream, "sha256").hexdigest()
        if actual != package["sha256"]:
            raise RuntimeError("Fixture changed after verification: " + str(source))
        shutil.copy2(source, guest / source.name)
        entries.append(f"package={package['package']}\nactivity=\n"
                       f"label={package['package']}\n"
                       f"apk=/data/local/tmp/muplar/gapps/{source.name}\n---\n")
    registry = WORK / "probe-packages.properties"
    registry.write_text("".join(entries))
    socket_name = f"playstore-{os.getpid()}.sock"
    sock = sysroot / "tmp" / socket_name
    logpath = WORK / "launch.log"
    with (WORK / "service.log").open("w") as service_log:
        service = subprocess.Popen([
            str(ROOT / "build/bin/muplard"), "--socket", str(sock),
            "--registry", str(registry), "--pid-file", str(WORK / "service.pid"),
            "--settings", str(WORK / "probe-settings.db"),
        ], stdout=service_log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 5
            while not sock.exists() and time.monotonic() < deadline:
                if service.poll() is not None:
                    raise RuntimeError("Test service exited; see service.log")
                time.sleep(0.05)
            if not sock.exists():
                raise RuntimeError("Test service socket did not appear")
            env = dict(os.environ, MUPLAR_SERVICE_SOCKET="/tmp/" + socket_name,
                       MUPLAR_ANDROID_SOFTWARE_FRAME_PATH=
                       "/data/local/tmp/muplar/frames/playstore-probe.mhr")
            with logpath.open("w") as log:
                process = subprocess.Popen([
                    str(ROOT / "build/bin/mup"), "--sysroot", str(sysroot),
                    "--apk", str(fixture / "PlayStore.apk"),
                ], env=env, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + args.timeout
                    while process.poll() is None and time.monotonic() < deadline:
                        if "failed to invoke onCreate" in logpath.read_text(errors="replace"):
                            time.sleep(0.2)
                            break
                        time.sleep(0.1)
                finally:
                    stop(process)
        finally:
            stop(service)
    text = logpath.read_text(errors="replace")
    result = {
        "application_on_create_failed": "Application onCreate failed" in text,
        "activity_on_create_failed": "failed to invoke onCreate" in text,
        "entered_main_looper": "entering main looper" in text,
        "native_crash": "MUPLAR CRASH HANDLER" in text,
        "unsupported_system_services": sorted(set(re.findall(
            r"unsupported system service: ([^\r\n]+)", text))),
        "log": str(logpath),
        "play_store_support_verified": False,
    }
    (WORK / "probe-result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    startup_completed = (result["entered_main_looper"]
                         and not result["application_on_create_failed"]
                         and not result["activity_on_create_failed"]
                         and not result["native_crash"])
    return 0 if startup_completed else 1


if __name__ == "__main__":
    raise SystemExit(main())
