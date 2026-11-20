#!/usr/bin/env python3
"""Prepare signature-verified Open GApps APKs for isolated compatibility tests.

This does not install apps or modify an Android instance. Run with an Android
SDK build-tools directory using --build-tools. Artifacts stay under build/.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[4]
BUNDLE_NAME = "open_gapps-arm64-11.0-pico-20220215.zip"
BUNDLE_URL = "https://downloads.sourceforge.net/project/opengapps/arm64/20220215/" + BUNDLE_NAME
BUNDLE_SHA256 = "722bf4395edc62c12ca521d71bb929157b714c240f2557a61d9a30969230ed36"
# Packaged Phonesky is recompressed and fails v2/v3 verification. The original
# source APK retains its signatures: https://github.com/opengapps/opengapps/issues/913
STORE_URL = ("https://gitlab.opengapps.org/api/v4/projects/opengapps%2Farm64/"
             "repository/files/priv-app%2Fcom.android.vending%2F21%2Fnodpi%2F83032110.apk/"
             "raw?ref=master&lfs=true")
STORE_SHA256 = "22e9e27d52883cfdfdcf9c32cd1a9676bda41e4060e00295d49aa870be0649e1"


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fetch(url, path, expected):
    if not path.exists():
        partial = path.with_suffix(path.suffix + ".partial")
        with urllib.request.urlopen(url, timeout=60) as source, partial.open("wb") as destination:
            shutil.copyfileobj(source, destination)
        if sha256(partial) != expected:
            raise RuntimeError("Downloaded artifact checksum mismatch: " + str(partial))
        partial.replace(path)
    if sha256(path) != expected:
        raise RuntimeError("Cached artifact checksum mismatch: " + str(path))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-tools", type=Path, required=True)
    args = parser.parse_args()
    aapt = args.build_tools.resolve() / "aapt2"
    signer = args.build_tools.resolve() / "apksigner"
    if not aapt.is_file() or not signer.is_file():
        parser.error("--build-tools must contain aapt2 and apksigner")
    work = ROOT / "build/opengapps"
    fixture = work / "fixture"
    fixture.mkdir(parents=True, exist_ok=True)
    bundle = fetch(BUNDLE_URL, work / BUNDLE_NAME, BUNDLE_SHA256)
    source = fetch(STORE_URL, work / "PlayStore-source-83032110.apk", STORE_SHA256)
    shutil.copyfile(source, fixture / "PlayStore.apk")
    components = [
        ("gsfcore-all", "nodpi", "GoogleServicesFramework", "GoogleServicesFramework.apk"),
        ("gmscore-arm64", "480", "PrebuiltGmsCore", "GooglePlayServices.apk"),
    ]
    with zipfile.ZipFile(bundle) as archive, tempfile.TemporaryDirectory(dir=work) as directory:
        for component, density, app, output in components:
            packed = Path(directory) / (component + ".tar.lz")
            packed.write_bytes(archive.read("Core/" + packed.name))
            member = f"{component}/{density}/priv-app/{app}/{app}.apk"
            with (fixture / output).open("wb") as destination:
                subprocess.run(["tar", "-xOf", str(packed), member], stdout=destination, check=True)
    packages = {
        "PlayStore.apk": "com.android.vending",
        "GooglePlayServices.apk": "com.google.android.gms",
        "GoogleServicesFramework.apk": "com.google.android.gsf",
    }
    metadata = {
        "purpose": "compatibility fixture; not a supported installation",
        "bundle": {"url": BUNDLE_URL, "sha256": BUNDLE_SHA256, "android": "11", "arch": "arm64"},
        "play_store_source": {"url": STORE_URL, "sha256": STORE_SHA256},
        "packages": [],
    }
    for name, expected in packages.items():
        apk = fixture / name
        result = subprocess.run([str(signer), "verify", "--print-certs", str(apk)],
                                text=True, capture_output=True)
        (fixture / (name + ".verification.txt")).write_text(result.stdout + result.stderr)
        result.check_returncode()
        result = subprocess.run([str(aapt), "dump", "badging", str(apk)],
                                text=True, capture_output=True, check=True)
        (fixture / (name + ".badging.txt")).write_text(result.stdout)
        match = re.search(r"^package: name='([^']+)' versionCode='([^']+)'", result.stdout, re.M)
        if not match or match[1] != expected:
            raise RuntimeError("Unexpected APK package: " + name)
        metadata["packages"].append({"file": name, "package": match[1],
                                     "version_code": int(match[2]), "sha256": sha256(apk)})
        print(f"Verified {match[1]} version {match[2]}", flush=True)
    (fixture / "provenance.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print("Prepared fixture:", fixture)


if __name__ == "__main__":
    main()
