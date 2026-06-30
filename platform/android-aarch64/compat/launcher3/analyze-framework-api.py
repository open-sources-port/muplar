#!/usr/bin/env python3
import pathlib
import re
import sys
import zipfile


def declared_classes(source_root: pathlib.Path) -> set[str]:
    classes: set[str] = set()
    package_pattern = re.compile(r"^\s*package\s+([A-Za-z0-9_.]+)")
    for path in source_root.rglob("*.java"):
        package = ""
        for line in path.read_text(errors="ignore").splitlines():
            match = package_pattern.match(line)
            if match:
                package = match.group(1)
                break
        if package:
            classes.add(f"{package}.{path.stem}")
    return classes


def android_jar_classes(path: pathlib.Path) -> set[str]:
    if not path.is_file():
        return set()
    with zipfile.ZipFile(path) as archive:
        return {
            name[:-6].replace("/", ".").split("$")[0]
            for name in archive.namelist()
            if name.startswith("android/") and name.endswith(".class")
        }


def resolve_import(value: str, known: set[str]) -> str:
    value = value.split(" as ", 1)[0].rstrip(";")
    candidate = value
    while candidate.startswith("android."):
        if candidate in known:
            return candidate
        if "." not in candidate:
            break
        candidate = candidate.rsplit(".", 1)[0]
    return value


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(f"Usage: {sys.argv[0]} LAUNCHER3_SOURCE [OUTPUT]", file=sys.stderr)
        return 2
    source = pathlib.Path(sys.argv[1]).resolve()
    output = pathlib.Path(sys.argv[2]).resolve() if len(sys.argv) == 3 else None
    root = pathlib.Path(__file__).resolve().parents[4]
    bootstrap = root / "platform/android-aarch64/java-bootstrap"
    if not (source / "Android.bp").is_file():
        print(f"Not a Launcher3 source checkout: {source}", file=sys.stderr)
        return 1

    imports: set[str] = set()
    pattern = re.compile(r"^\s*import\s+(android\.[A-Za-z0-9_.$]+)")
    provided = declared_classes(bootstrap)
    sdk_jar = pathlib.Path("/opt/android/sdk/platforms/android-36.1/android.jar")
    known = provided | android_jar_classes(sdk_jar)
    for extension in ("*.java", "*.kt"):
        for path in source.rglob(extension):
            for line in path.read_text(errors="ignore").splitlines():
                match = pattern.match(line)
                if match:
                    imports.add(resolve_import(match.group(1), known))

    missing = sorted(imports - provided)
    covered = sorted(imports & provided)
    report = [
        f"source={source}",
        f"androidImportedTypes={len(imports)}",
        f"bootstrapCoveredTypes={len(covered)}",
        f"bootstrapMissingTypes={len(missing)}",
        f"sdkClassIndex={sdk_jar}",
        "",
        "[missing]",
        *missing,
        "",
        "[covered]",
        *covered,
        "",
    ]
    text = "\n".join(report)
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text)
        print(f"[Launcher3] API report: {output}")
    print("\n".join(report[:4]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
