import os
import re

cpp_path = "platform/android-aarch64/android/android_runtime.cpp"
out_dir = "tools/guest-egl-gles-stubs"
os.makedirs(out_dir, exist_ok=True)

# Parse HVC constants
hvc_map = {}
with open(cpp_path, 'r') as f:
    content = f.read()

# Match: static constexpr uint32_t HVC_NAME = value;
for match in re.finditer(r'static\s+constexpr\s+uint32_t\s+(HVC_[A-Z0-9_]+)\s*=\s*(0x[0-9a-fA-F]+|\d+);', content):
    name = match.group(1)
    val_str = match.group(2)
    val = int(val_str, 16) if val_str.startswith('0x') else int(val_str)
    hvc_map[name] = val

# Match: add("libEGL.so", "symbol", HVC_CONST, ...) or add("libGLESv2.so", "symbol", HVC_CONST, ...)
stubs = {"libEGL.so": [], "libGLESv2.so": []}
for match in re.finditer(r'add\(\"(libEGL\.so|libGLESv2\.so)\"\,\s*\"([A-Za-z0-9_]+)\"\,\s*(HVC_[A-Z0-9_]+)', content):
    lib = match.group(1)
    sym = match.group(2)
    hvc_const = match.group(3)
    if hvc_const in hvc_map:
        stubs[lib].append((sym, hvc_map[hvc_const]))
    else:
        print(f"Warning: constant {hvc_const} not found in map")

# Generate files
special_gles = {
    "glClearColor": """void glClearColor(float r, float g, float b, float a) {
    union { float f; uint64_t u; } ur, ug, ub, ua;
    ur.f = r; ug.f = g; ub.f = b; ua.f = a;
    bridge_call(0x2503, ur.u, ug.u, ub.u, ua.u, 0, 0, 0, 0);
}""",
    "glClearDepthf": """void glClearDepthf(float depth) {
    union { float f; uint64_t u; } ud;
    ud.f = depth;
    bridge_call(0x2504, ud.u, 0, 0, 0, 0, 0, 0, 0);
}""",
    "glVertexAttrib4f": """void glVertexAttrib4f(unsigned int index, float x, float y, float z, float w) {
    union { float f; uint64_t u; } ux, uy, uz, uw;
    ux.f = x; uy.f = y; uz.f = z; uw.f = w;
    bridge_call(0x2524, index, ux.u, uy.u, uz.u, uw.u, 0, 0, 0);
}""",
    "glUniform1f": """void glUniform1f(int location, float v) {
    union { float f; uint64_t u; } uv;
    uv.f = v;
    bridge_call(0x252c, location, uv.u, 0, 0, 0, 0, 0, 0);
}""",
    "glUniform4f": """void glUniform4f(int location, float x, float y, float z, float w) {
    union { float f; uint64_t u; } ux, uy, uz, uw;
    ux.f = x; uy.f = y; uz.f = z; uw.f = w;
    bridge_call(0x252e, location, ux.u, uy.u, uz.u, uw.u, 0, 0, 0);
}"""
}

def generate_lib_c(lib_name, file_name):
    c_code = []
    c_code.append("#include <stdint.h>\n")
    c_code.append("""#if defined(__x86_64__)
#include <unistd.h>
#include <sys/syscall.h>

static inline uint64_t bridge_call(uint32_t nr, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7};
    return syscall(999, (uint64_t)nr, (uint64_t)args);
}
#elif defined(__aarch64__)
static inline uint64_t bridge_call(uint32_t nr, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    register uint64_t r0 __asm__("x0") = arg0;
    register uint64_t r1 __asm__("x1") = arg1;
    register uint64_t r2 __asm__("x2") = arg2;
    register uint64_t r3 __asm__("x3") = arg3;
    register uint64_t r4 __asm__("x4") = arg4;
    register uint64_t r5 __asm__("x5") = arg5;
    register uint64_t r6 __asm__("x6") = arg6;
    register uint64_t r7 __asm__("x7") = arg7;
    register uint64_t r8 __asm__("x8") = nr;
    __asm__ volatile("hvc #6"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r6), "r"(r7), "r"(r8)
                     : "memory");
    return r0;
}
#endif
""")
    
    # Sort to keep generated code deterministic
    sorted_stubs = sorted(stubs[lib_name], key=lambda x: x[0])
    for sym, val in sorted_stubs:
        if sym in special_gles:
            override_code = special_gles[sym].replace("0x2503", f"0x{val:X}").replace("0x2504", f"0x{val:X}").replace("0x2524", f"0x{val:X}").replace("0x252c", f"0x{val:X}").replace("0x252e", f"0x{val:X}")
            c_code.append(override_code + "\n")
        else:
            c_code.append(f"""uint64_t {sym}(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {{
    return bridge_call(0x{val:X}, a0, a1, a2, a3, a4, a5, a6, a7);
}}
""")
            
    with open(os.path.join(out_dir, file_name), 'w') as out_f:
        out_f.write("\n".join(c_code))
    print(f"Generated {file_name} with {len(sorted_stubs)} stubs.")

generate_lib_c("libEGL.so", "libEGL.c")
generate_lib_c("libGLESv2.so", "libGLESv2.c")
