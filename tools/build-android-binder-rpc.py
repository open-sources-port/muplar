#!/usr/bin/env python3
"""Build the experimental Binder RPC adapter against an isolated Android rootfs.

Headers are pinned to AOSP commits and content hashes. Pass --fetch-headers on
first use; the adapter is staged only if --output explicitly selects a rootfs.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "platform/android-aarch64/binder-rpc"


def header_digest(entries):
    digest = hashlib.sha256()
    for name, data in sorted(entries):
        digest.update(name.encode() + b"\0" + data + b"\0")
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sysroot", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--fetch-headers", action="store_true")
    args = parser.parse_args()
    sysroot = args.sysroot.resolve()
    work = ROOT / "build/binder-rpc"
    work.mkdir(parents=True, exist_ok=True)
    include = work / "include"
    manifest = (SOURCE / "headers.json").read_bytes()
    marker = work / "headers-manifest.sha256"
    expected = hashlib.sha256(manifest).hexdigest()
    content_file = work / "headers-content.json"
    if args.fetch_headers:
        if include.exists():
            shutil.rmtree(include)
        include.mkdir()
        for source in json.loads(manifest):
            url = (source["repository"] + "/+archive/" + source["commit"]
                   + "/" + source["path"] + ".tar.gz")
            data = subprocess.check_output(["curl", "--fail", "--location", "--silent",
                                            "--show-error", "--max-time", "60", url])
            entries = []
            with tarfile.open(fileobj=io.BytesIO(data)) as archive:
                for entry in archive.getmembers():
                    if not entry.isfile():
                        continue
                    path = PurePosixPath(entry.name)
                    if path.is_absolute() or ".." in path.parts:
                        raise RuntimeError("Unsafe header archive path")
                    entries.append((entry.name, archive.extractfile(entry).read()))
            if header_digest(entries) != source["content_sha256"]:
                raise RuntimeError("Header content hash mismatch: " + url)
            for name, content in entries:
                target = include / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(content)
        content_file.write_text(json.dumps({
            str(path.relative_to(include)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(include.rglob("*")) if path.is_file()
        }, sort_keys=True))
        marker.write_text(expected)
    if not marker.is_file() or marker.read_text() != expected:
        parser.error("Pinned headers missing or changed; use --fetch-headers")
    if not content_file.is_file():
        parser.error("Header content inventory missing; use --fetch-headers")
    inventory = json.loads(content_file.read_text())
    current = {str(path.relative_to(include)) for path in include.rglob("*") if path.is_file() or path.is_symlink()}
    if current != set(inventory) or any(
            (include / name).is_symlink() or hashlib.sha256((include / name).read_bytes()).hexdigest() != digest
            for name, digest in inventory.items()):
        parser.error("Cached header content changed; use --fetch-headers")
    ndk = os.environ.get("ANDROID_NDK_HOME", "/opt/homebrew/share/android-ndk")
    candidates = [Path(ndk) / "toolchains/llvm/prebuilt" / host / "bin/aarch64-linux-android35-clang++"
                  for host in ("darwin-arm64", "darwin-x86_64", "linux-x86_64")]
    compiler = next((path for path in candidates if path.is_file()), None)
    if compiler is None:
        parser.error("Android NDK compiler missing; set ANDROID_NDK_HOME")
    # Android's platform libc++ uses __1; the NDK uses the separate __ndk1 ABI.
    # Keep the NDK's other configuration and link the actual platform libc++.
    config = compiler.parent.parent / "sysroot/usr/include/c++/v1/__config_site"
    cxx = work / "cxx"
    cxx.mkdir(exist_ok=True)
    (cxx / "__config_site").write_text(config.read_text().replace("__ndk1", "__1"))
    library_dirs = [sysroot / "system/lib64", sysroot / "apex/com.android.art/lib64"]
    if not (library_dirs[0] / "libbinder.so").is_file():
        parser.error("The selected rootfs has no native libbinder.so")
    output = (args.output or work / "libmuplar_binder_rpc.so").resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        str(compiler), "-std=c++20", "-fno-rtti", "-fPIC", "-shared", "-Wall", "-Wextra",
        "-I", str(cxx), "-I", str(include), "-nostdlib++", str(SOURCE / "muplar_binder_rpc.cpp"),
        *["-L" + str(path) for path in library_dirs],
        "-lbinder", "-lutils", "-l:libc++.so", "-o", str(output),
    ], check=True)
    print(output)


if __name__ == "__main__":
    main()
