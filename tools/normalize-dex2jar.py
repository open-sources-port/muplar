#!/usr/bin/env python3
import pathlib
import struct
import sys
import zipfile


def constant_pool(data: bytes):
    count = struct.unpack_from(">H", data, 8)[0]
    entries = [None] * count
    offsets = [None] * count
    offset = 10
    index = 1
    while index < count:
        tag = data[offset]
        offsets[index] = offset
        if tag == 1:
            size = struct.unpack_from(">H", data, offset + 1)[0]
            entries[index] = (tag, data[offset + 3 : offset + 3 + size].decode(
                "utf-8", errors="replace"))
            offset += 3 + size
        elif tag in (3, 4):
            offset += 5
        elif tag in (5, 6):
            offset += 9
            index += 1
        elif tag in (7, 8, 16, 19, 20):
            entries[index] = (tag, struct.unpack_from(">H", data, offset + 1)[0])
            offset += 3
        elif tag in (9, 10, 11, 12, 17, 18):
            entries[index] = (tag,) + struct.unpack_from(">HH", data, offset + 1)
            offset += 5
        elif tag == 15:
            offset += 4
        else:
            raise ValueError(f"unsupported constant-pool tag {tag}")
        index += 1
    return entries, offsets, offset


def class_name(entries, class_index: int) -> str:
    class_entry = entries[class_index]
    if not class_entry or class_entry[0] != 7:
        return ""
    name_entry = entries[class_entry[1]]
    return name_entry[1] if name_entry and name_entry[0] == 1 else ""


def interface_name(data: bytes):
    entries, _, class_offset = constant_pool(data)
    access_flags, this_class = struct.unpack_from(">HH", data, class_offset)
    return class_name(entries, this_class) if access_flags & 0x0200 else None


def normalize_class(data: bytes, interfaces: set[str]):
    changed_version = False
    methodrefs = 0
    major = struct.unpack_from(">H", data, 6)[0]
    mutable = bytearray(data)
    if major < 52:
        struct.pack_into(">H", mutable, 6, 52)
        changed_version = True
    entries, offsets, _ = constant_pool(data)
    for index, entry in enumerate(entries):
        if entry and entry[0] == 10 and class_name(entries, entry[1]) in interfaces:
            mutable[offsets[index]] = 11
            methodrefs += 1
    return bytes(mutable), changed_version, methodrefs


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} JAR", file=sys.stderr)
        return 2
    jar = pathlib.Path(sys.argv[1])
    temporary = jar.with_suffix(jar.suffix + ".normalized.tmp")
    changed = 0
    rewritten_methodrefs = 0
    with zipfile.ZipFile(jar, "r") as source, zipfile.ZipFile(
        temporary, "w", allowZip64=True
    ) as destination:
        contents = {info.filename: source.read(info.filename)
                    for info in source.infolist()}
        interfaces = {
            name
            for filename, data in contents.items()
            if filename.endswith(".class") and data[:4] == b"\xca\xfe\xba\xbe"
            for name in [interface_name(data)]
            if name
        }
        # dex2jar cannot inspect JDK classes bundled outside the APK and may
        # encode static Java 8 interface methods as Methodref entries.
        interfaces.add("java/util/Comparator")
        interfaces.add("java/util/stream/Stream")
        interfaces.update({"java/util/List", "java/util/Set", "java/util/Map"})
        for info in source.infolist():
            data = contents[info.filename]
            if info.filename.endswith(".class") and data[:4] == b"\xca\xfe\xba\xbe":
                data, version_changed, methodrefs = normalize_class(data, interfaces)
                changed += int(version_changed)
                rewritten_methodrefs += methodrefs
            destination.writestr(info, data)
    temporary.replace(jar)
    marker = pathlib.Path(str(jar) + ".normalized-v3")
    marker.write_text(
        f"classVersion=52\nchanged={changed}\n"
        f"interfaceMethodrefs={rewritten_methodrefs}\n"
    )
    print(
        f"[dex2jar] normalized {changed} class versions to Java 8; "
        f"rewrote {rewritten_methodrefs} interface method references"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
