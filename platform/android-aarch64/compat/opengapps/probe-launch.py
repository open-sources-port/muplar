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
import struct
import zlib
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


def capture_frame(sysroot, started):
    candidates = [ROOT / "muplar/frames/playstore-probe.mhr",
                  sysroot / "data/local/tmp/muplar/frames/playstore-probe.mhr"]
    for path in candidates:
        if not path.is_file() or path.stat().st_mtime_ns < started:
            continue
        raw = path.read_bytes()
        if len(raw) < 24:
            continue
        magic, width, height, stride, size = struct.unpack("<IIIIQ", raw[:24])
        if not (0 < width <= 4096 and 0 < height <= 4096 and stride >= width
                and size == stride * height * 4 and len(raw) == size + 24):
            continue
        rgba = b"".join(raw[24 + y * stride * 4:24 + y * stride * 4 + width * 4]
                        for y in range(height))
        uniform = rgba == rgba[:4] * (width * height)
        def chunk(kind, data):
            return (struct.pack(">I", len(data)) + kind + data
                    + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))
        scans = b"".join(b"\x00" + rgba[y * width * 4:(y + 1) * width * 4]
                         for y in range(height))
        png = (b"\x89PNG\r\n\x1a\n"
               + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
               + chunk(b"IDAT", zlib.compress(scans)) + chunk(b"IEND", b""))
        (WORK / "playstore-probe.mhr").write_bytes(raw)
        output = WORK / "playstore-probe.png"
        output.write_bytes(png)
        return {"fresh": True, "width": width, "height": height,
                "uniform": uniform, "png": str(output)}
    return {"fresh": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sysroot", required=True, type=Path)
    parser.add_argument("--timeout", default=25, type=int)
    parser.add_argument("--debug-views", action="store_true")
    parser.add_argument("--fresh-data", action="store_true",
                        help="Back up Play Store data in the dedicated rootfs before probing")
    args = parser.parse_args()
    sysroot = args.sysroot.resolve()
    if ROOT / "build" not in sysroot.parents:
        parser.error("Use a dedicated rootfs copy under build/, not a running instance")
    if not (sysroot / "system/framework/framework.jar").is_file():
        parser.error("Android framework.jar missing from the test rootfs")
    data_backup = None
    if args.fresh_data:
        data = sysroot / "data/user/0/com.android.vending"
        if not data.resolve().is_relative_to(sysroot):
            parser.error("Play Store data escapes the dedicated rootfs")
        if data.exists():
            data_backup = data.with_name(data.name + ".probe-backup-" + str(time.time_ns()))
            data.rename(data_backup)
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
    started = time.time_ns()
    exit_code_before_stop = None
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
            if args.debug_views:
                env["MUPLAR_DEBUG_VIEWS"] = "all"
            with logpath.open("w") as log:
                process = subprocess.Popen([
                    str(ROOT / "build/bin/mup"), "--sysroot", str(sysroot),
                    "--apk", str(fixture / "PlayStore.apk"),
                ], env=env, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + args.timeout
                    while process.poll() is None and time.monotonic() < deadline:
                        text = logpath.read_text(errors="replace")
                        if ("Application onCreate failed" in text
                                or "failed to invoke onCreate" in text
                                or "main looper exited:" in text
                                or "MUPLAR CRASH HANDLER" in text):
                            time.sleep(0.2)
                            break
                        time.sleep(0.1)
                finally:
                    exit_code_before_stop = process.poll()
                    stop(process)
        finally:
            stop(service)
    text = logpath.read_text(errors="replace")
    result = {
        "fresh_data": args.fresh_data,
        "data_backup": str(data_backup) if data_backup else None,
        "frame": capture_frame(sysroot, started),
        "guest_thread_exhaustion": "clone_thread: thread table full" in text,
        "binder_transport": "rpc" if "[Muplar/BinderRPC] native socket transport initialized" in text else "kernel-unavailable",
        "application_on_create_failed": "Application onCreate failed" in text,
        "activity_on_create_failed": "failed to invoke onCreate" in text,
        "entered_main_looper": "entering main looper" in text,
        "application_on_create_completed": "Application onCreate completed class=" in text,
        "activity_on_create_completed": "onCreate completed successfully" in text,
        "main_looper_failed": "main looper exited:" in text,
        "native_crash": "MUPLAR CRASH HANDLER" in text,
        "process_exit_code_before_stop": exit_code_before_stop,
        "startup_timed_out": (exit_code_before_stop is None
            and "entering main looper" not in text
            and "Application onCreate failed" not in text
            and "failed to invoke onCreate" not in text
            and "main looper exited:" not in text
            and "MUPLAR CRASH HANDLER" not in text),
        "observation_timeout_seconds": args.timeout,
        "unsupported_system_services": sorted(set(re.findall(
            r"unsupported system service: ([^\r\n]+)", text))),
        "log": str(logpath),
        "play_store_support_verified": False,
    }
    (WORK / "probe-result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    startup_completed = (result["entered_main_looper"]
                         and result["application_on_create_completed"]
                         and result["activity_on_create_completed"]
                         and not result["main_looper_failed"]
                         and not result["application_on_create_failed"]
                         and not result["activity_on_create_failed"]
                         and not result["native_crash"]
                         and not result["guest_thread_exhaustion"])
    return 0 if startup_completed else 1


if __name__ == "__main__":
    raise SystemExit(main())
