#!/usr/bin/env python3
import pathlib
import struct
import sys
import zipfile


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} JAR", file=sys.stderr)
        return 2
    jar = pathlib.Path(sys.argv[1])
    temporary = jar.with_suffix(jar.suffix + ".normalized.tmp")
    changed = 0
    with zipfile.ZipFile(jar, "r") as source, zipfile.ZipFile(
        temporary, "w", allowZip64=True
    ) as destination:
        for info in source.infolist():
            data = source.read(info.filename)
            if info.filename.endswith(".class") and data[:4] == b"\xca\xfe\xba\xbe":
                major = struct.unpack_from(">H", data, 6)[0]
                if major < 52:
                    data = data[:6] + struct.pack(">H", 52) + data[8:]
                    changed += 1
            destination.writestr(info, data)
    temporary.replace(jar)
    marker = pathlib.Path(str(jar) + ".normalized-v1")
    marker.write_text(f"classVersion=52\nchanged={changed}\n")
    print(f"[dex2jar] normalized {changed} class versions to Java 8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
