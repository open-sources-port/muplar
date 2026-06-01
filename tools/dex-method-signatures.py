#!/usr/bin/env python3
import struct
import sys
import zipfile


def usage():
    print("Usage: dex-method-signatures.py APK CLASS METHOD", file=sys.stderr)
    print("CLASS uses slash form, for example com/example/Foo.", file=sys.stderr)


def read_uleb128(data, offset):
    result = 0
    shift = 0
    while True:
        value = data[offset]
        offset += 1
        result |= (value & 0x7F) << shift
        if value < 0x80:
            return result, offset
        shift += 7


def read_string(data, offset):
    _, offset = read_uleb128(data, offset)
    end = data.index(0, offset)
    return data[offset:end].decode("utf-8", "replace")


def parse_dex_methods(data):
    if len(data) < 0x70 or not data.startswith(b"dex\n"):
        return []

    def u32(offset):
        return struct.unpack_from("<I", data, offset)[0]

    string_ids_size = u32(0x38)
    string_ids_off = u32(0x3C)
    type_ids_size = u32(0x40)
    type_ids_off = u32(0x44)
    proto_ids_size = u32(0x48)
    proto_ids_off = u32(0x4C)
    method_ids_size = u32(0x58)
    method_ids_off = u32(0x5C)

    strings = []
    for i in range(string_ids_size):
        string_data_off = u32(string_ids_off + i * 4)
        strings.append(read_string(data, string_data_off))

    types = []
    for i in range(type_ids_size):
        descriptor_idx = u32(type_ids_off + i * 4)
        types.append(strings[descriptor_idx])

    protos = []
    for i in range(proto_ids_size):
        base = proto_ids_off + i * 12
        return_type_idx = u32(base + 4)
        parameters_off = u32(base + 8)
        params = ""
        if parameters_off:
            size = u32(parameters_off)
            param_types = []
            for j in range(size):
                type_idx = struct.unpack_from("<H", data, parameters_off + 4 + j * 2)[0]
                param_types.append(types[type_idx])
            params = "".join(param_types)
        protos.append(f"({params}){types[return_type_idx]}")

    methods = []
    for i in range(method_ids_size):
        base = method_ids_off + i * 8
        class_idx, proto_idx = struct.unpack_from("<HH", data, base)
        name_idx = u32(base + 4)
        methods.append((types[class_idx], strings[name_idx], protos[proto_idx]))
    return methods


def descriptor_for_class(class_name):
    name = class_name.strip()
    if name.startswith("L") and name.endswith(";"):
        return name
    return f"L{name.strip('/')};"


def main(argv):
    if len(argv) != 4:
        usage()
        return 2

    apk, class_name, method_name = argv[1:4]
    target_class = descriptor_for_class(class_name)
    found = set()

    with zipfile.ZipFile(apk) as zf:
        dex_names = sorted(
            name for name in zf.namelist()
            if name == "classes.dex" or
            (name.startswith("classes") and name.endswith(".dex"))
        )
        for dex_name in dex_names:
            methods = parse_dex_methods(zf.read(dex_name))
            for cls, name, sig in methods:
                if cls == target_class and name == method_name:
                    found.add(sig)

    for sig in sorted(found):
        print(sig)
    return 0 if found else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
