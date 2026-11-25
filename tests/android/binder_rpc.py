#!/usr/bin/env python3
"""Verify socket Binder RPC calls, callbacks, errors and death across two ART guests."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def wait_marker(process, log, marker, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        text = log.read_text(errors="replace")
        if any(failure in text for failure in (
                "BINDER_RPC_SMOKE_FAIL", "MUPLAR CRASH HANDLER",
                "failed to invoke onCreate", "runtime shim load failed")):
            raise RuntimeError("RPC guest failed; see " + str(log))
        if marker in text:
            return
        if process.poll() is not None:
            raise RuntimeError("RPC guest exited; see " + str(log))
        time.sleep(0.1)
    raise RuntimeError("RPC guest timed out; see " + str(log))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sysroot", required=True, type=Path)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    sysroot = args.sysroot.resolve()
    if ROOT / "build" not in sysroot.parents:
        parser.error("Use a dedicated rootfs under build/")
    work = ROOT / "build/android-binder-rpc-test"
    if not all((work / (role + ".apk")).is_file() for role in ("server", "client")):
        parser.error("Build tests/assets/android-binder-rpc/build-apk.sh first")
    rpc_socket = sysroot / "tmp/brpc-test.sock"
    if rpc_socket.exists():
        parser.error("RPC test socket already exists; remove it after stopping its owner")
    socket_name = f"brpc-{os.getpid()}.sock"
    service_socket = sysroot / "tmp" / socket_name
    server = client = service = None
    result = {"passed": False, "remote_calls": False, "remote_death": False}
    with (work / "service.log").open("w") as service_log, \
            (work / "server.log").open("w") as server_log, \
            (work / "client.log").open("w") as client_log:
        try:
            service = subprocess.Popen([
                str(ROOT / "build/bin/muplard"), "--socket", str(service_socket),
                "--registry", str(work / "packages.properties"),
                "--pid-file", str(work / "service.pid"),
                "--settings", str(work / "settings.db"),
            ], stdout=service_log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 5
            while not service_socket.exists() and time.monotonic() < deadline:
                if service.poll() is not None:
                    raise RuntimeError("Private daemon exited; see service.log")
                time.sleep(0.05)
            if not service_socket.exists():
                raise RuntimeError("Private daemon did not start")
            env = dict(os.environ, MUPLAR_SERVICE_SOCKET="/tmp/" + socket_name)
            command = [str(ROOT / "build/bin/mup")] + (["--verbose"] if args.verbose else []) + ["--sysroot", str(sysroot), "--apk"]
            server = subprocess.Popen(command + [str(work / "server.apk")],
                                      env=env, stdout=server_log, stderr=subprocess.STDOUT)
            wait_marker(server, work / "server.log", "BINDER_RPC_SERVER_READY", 60)
            client = subprocess.Popen(command + [str(work / "client.apk")],
                                      env=env, stdout=client_log, stderr=subprocess.STDOUT)
            wait_marker(client, work / "client.log", "BINDER_RPC_TRANSACTIONS_PASS", 90)
            result["remote_calls"] = True
            stop(server)
            wait_marker(client, work / "client.log", "BINDER_RPC_SMOKE_PASS", 30)
            result.update(passed=True, remote_death=True)
        except RuntimeError as error:
            result["error"] = str(error)
        finally:
            stop(client)
            stop(server)
            stop(service)
            if rpc_socket.exists():
                rpc_socket.unlink()
    (work / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
