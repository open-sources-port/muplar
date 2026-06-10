#!/bin/zsh
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WAWONA_SRC="${ROOT_DIR}/third_party/wawona"
BUILD_DIR="${ROOT_DIR}/build"
OUTPUT_BIN="${BUILD_DIR}/bin/wawona"
OBJ_BUILD_DIR="${BUILD_DIR}/wawona-macos-objc"

CARGO_BIN="cargo"

# Check if cargo is in PATH
if ! command -v cargo &> /dev/null; then
    # Check if cargo is in ~/.cargo/bin
    if [ -x "$HOME/.cargo/bin/cargo" ]; then
        CARGO_BIN="$HOME/.cargo/bin/cargo"
    else
        echo "Cargo not found. Attempting to install Rust via rustup..."
        curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
        # Make sure cargo is available at ~/.cargo/bin
        CARGO_BIN="$HOME/.cargo/bin/cargo"
        if [ ! -x "$CARGO_BIN" ]; then
            echo "ERROR: Failed to install Rust/Cargo."
            exit 1
        fi
    fi
fi

echo "Using cargo: $CARGO_BIN"

cd "$WAWONA_SRC"

# If waypipe directory does not exist, download and patch it
if [ ! -d "waypipe" ]; then
    echo "Cloning waypipe v0.11.0..."
    git clone --depth 1 --branch v0.11.0 https://gitlab.freedesktop.org/mstoeckl/waypipe.git waypipe
    echo "Patching waypipe source..."
    
    # Create a temporary sed wrapper using Python for robust cross-platform parsing
    TMP_BIN_DIR="${BUILD_DIR}/tmp-bin"
    mkdir -p "$TMP_BIN_DIR"
    cat > "${TMP_BIN_DIR}/sed" <<'EOF'
#!/usr/bin/env python3
import sys
import re
import os

args = sys.argv[1:]
in_place = False
backup_ext = ''

clean_args = []
i = 0
while i < len(args):
    arg = args[i]
    if arg == '-i':
        in_place = True
        i += 1
        if i < len(args) and (args[i] == '' or args[i] == '.bak'):
            backup_ext = args[i]
            i += 1
    elif arg.startswith('-i'):
        in_place = True
        backup_ext = arg[2:]
        i += 1
    else:
        clean_args.append(arg)
        i += 1

if not clean_args:
    sys.exit(0)

script = clean_args[0]
files = clean_args[1:]

def apply_script(content, script):
    if script.startswith('1i'):
        text = script[2:]
        text = text.replace('\\n', '\n')
        return text + '\n' + content

    for sep in ['/', '#', '|']:
        m = re.match(r'^s' + re.escape(sep) + r'(.*)' + re.escape(sep) + r'(.*)' + re.escape(sep) + r'([g]*)$', script)
        if m:
            pattern, repl, flags = m.groups()
            
            py_pattern = ""
            idx = 0
            while idx < len(pattern):
                if pattern[idx:idx+2] == '\\(':
                    py_pattern += '('
                    idx += 2
                elif pattern[idx:idx+2] == '\\)':
                    py_pattern += ')'
                    idx += 2
                elif pattern[idx:idx+2] == '\\/':
                    py_pattern += '/'
                    idx += 2
                else:
                    py_pattern += pattern[idx]
                    idx += 1
            
            py_repl = ""
            idx = 0
            while idx < len(repl):
                if repl[idx:idx+2] == '\\/':
                    py_repl += '/'
                    idx += 2
                elif repl[idx] == '\\' and idx + 1 < len(repl) and repl[idx+1].isdigit():
                    py_repl += '\\g<' + repl[idx+1] + '>'
                    idx += 2
                else:
                    py_repl += repl[idx]
                    idx += 1
            
            count = 0 if 'g' in flags else 1
            re_flags = 0
            if 'i' in flags:
                re_flags |= re.IGNORECASE
                
            try:
                return re.sub(py_pattern, py_repl, content, count=count, flags=re_flags)
            except Exception:
                return content.replace(pattern, repl)

    return content

for file_path in files:
    if not os.path.exists(file_path):
        continue
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    new_content = apply_script(content, script)
    
    if in_place:
        if backup_ext:
            backup_path = file_path + backup_ext
            with open(backup_path, 'w', encoding='utf-8') as f:
                f.write(content)
        with open(file_path, 'w', encoding='utf-8') as f:
            f.write(new_content)
    else:
        sys.stdout.write(new_content)
EOF
    chmod +x "${TMP_BIN_DIR}/sed"
    
    # Add temporary bin to PATH for the patch script
    ORIG_PATH="$PATH"
    export PATH="${TMP_BIN_DIR}:$PATH"
    
    cd waypipe
    bash ../dependencies/libs/waypipe/patch-waypipe-source.sh
    cd ..
    
    # Restore PATH and clean up tmp-bin
    export PATH="$ORIG_PATH"
    rm -rf "$TMP_BIN_DIR"
fi

# Dynamically patch Cargo.toml to inject wayland-sys dependency with dlopen feature
python3 -c "
import pathlib
p = pathlib.Path('Cargo.toml')
content = p.read_text()
pathlib.Path('Cargo.toml.orig').write_text(content) # Keep a backup in a separate file
lock = pathlib.Path('Cargo.lock')
if lock.exists():
    pathlib.Path('Cargo.lock.orig').write_text(lock.read_text())
modified = content
if '[dependencies]' in modified:
    # Inject wayland-sys with dlopen feature right under [dependencies]
    modified = modified.replace('[dependencies]', '[dependencies]\nwayland-sys = { version = \"0.31\", features = [\"dlopen\"] }')
p.write_text(modified)
"

# Set up a cleanup trap to restore Cargo.toml/Cargo.lock on exit/failure
cleanup() {
    echo "Restoring Cargo.toml/Cargo.lock to original state..."
    if [ -f "Cargo.toml.orig" ]; then
        mv -f Cargo.toml.orig Cargo.toml
    fi
    if [ -f "Cargo.lock.orig" ]; then
        mv -f Cargo.lock.orig Cargo.lock
    fi
}
trap cleanup EXIT INT TERM

# Add Homebrew search paths for compiler/linker on Apple Silicon macOS
export LIBRARY_PATH="/opt/homebrew/lib:$LIBRARY_PATH"
export CPATH="/opt/homebrew/include:$CPATH"
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH"

# Run cargo build --release. Cargo's `wawona` binary is a platform stub; the
# macOS compositor entrypoint is Objective-C and links against libwawona.a.
"$CARGO_BIN" build --release

mkdir -p "${BUILD_DIR}/bin"
if [ ! -f "target/release/libwawona.a" ]; then
    echo "ERROR: Wawona static library not found at target/release/libwawona.a"
    exit 1
fi

echo "Building Wawona macOS compositor entrypoint..."

SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
CC="$(xcrun --sdk macosx -find clang)"

rm -rf "$OBJ_BUILD_DIR"
mkdir -p "$OBJ_BUILD_DIR"

objc_sources=(
    "${ROOT_DIR}/platform/commons/supervisor/wawona_host_main.m"
    src/platform/macos/WWNCompositorBridge.m
    src/platform/macos/WWNSettings.m
    src/platform/macos/WWNPlatformCallbacks.m
    src/platform/macos/ui/Helpers/WWNImageLoader.m
    src/platform/macos/ui/Machines/WWNMachineProfileStore.m
    src/platform/macos/ui/Machines/WWNMachinesCoordinator.m
    src/platform/macos/ui/Settings/WWNPreferences.m
    src/platform/macos/ui/Settings/WWNPreferencesManager.m
    src/platform/macos/ui/About/WWNAboutPanel.m
    src/platform/macos/ui/Settings/WWNSettingsModel.m
    src/platform/macos/ui/Settings/WWNWaypipeRunner.m
    src/platform/macos/ui/Settings/WWNSSHClient.m
    src/platform/macos/ui/Settings/WWNSettingsSplitViewController.m
    src/platform/macos/ui/Settings/WWNSettingsSidebarViewController.m
    src/platform/macos/WWNWindow.m
    src/platform/macos/WWNPopupWindow.m
)

c_sources=(
    src/platform/macos/WWNSettings.c
)

common_includes=(
    -Isrc
    -Isrc/util
    -Isrc/platform/macos
    -Isrc/platform/macos/ui
    -Isrc/platform/macos/ui/Helpers
    -Isrc/platform/macos/ui/Machines
    -Isrc/platform/macos/ui/Settings
    -I.
    -I/opt/homebrew/include
)

common_defs=(
    -DUSE_RUST_CORE=1
    -DWAWONA_VERSION=\"0.0.1\"
    -DWAWONA_WAYLAND_VERSION=\"unknown\"
    -DWAWONA_XKBCOMMON_VERSION=\"unknown\"
    -DWAWONA_LZ4_VERSION=\"unknown\"
    -DWAWONA_ZSTD_VERSION=\"unknown\"
    -DWAWONA_LIBFFI_VERSION=\"unknown\"
    -DWAWONA_SSHPASS_VERSION=\"unknown\"
    -DWAWONA_WAYPIPE_VERSION=\"unknown\"
)

warn_suppress=(
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-implicit-float-conversion
    -Wno-deprecated-declarations
    -Wno-format-nonliteral
    -Wno-format-pedantic
    -Wno-unguarded-availability-new
)

obj_files=()
for src_file in "${objc_sources[@]}"; do
    if [ ! -f "$src_file" ]; then
        echo "ERROR: Wawona source not found: $src_file"
        exit 1
    fi
    obj_file="${OBJ_BUILD_DIR}/${src_file//\//_}.o"
    "$CC" -c "$src_file" \
        -o "$obj_file" \
        -isysroot "$SDKROOT" \
        -mmacosx-version-min=14.0 \
        "${common_includes[@]}" \
        "${common_defs[@]}" \
        "${warn_suppress[@]}" \
        -fobjc-arc \
        -fPIC \
        -O2 \
        -DNDEBUG
    obj_files+=("$obj_file")
done

for src_file in "${c_sources[@]}"; do
    if [ ! -f "$src_file" ]; then
        echo "ERROR: Wawona source not found: $src_file"
        exit 1
    fi
    obj_file="${OBJ_BUILD_DIR}/${src_file//\//_}.o"
    "$CC" -c "$src_file" \
        -o "$obj_file" \
        -isysroot "$SDKROOT" \
        -mmacosx-version-min=14.0 \
        "${common_includes[@]}" \
        "${common_defs[@]}" \
        "${warn_suppress[@]}" \
        -fPIC \
        -O2 \
        -DNDEBUG
    obj_files+=("$obj_file")
done

link_libs=(
    -L/opt/homebrew/lib
    -L/opt/homebrew/opt/openssl@3/lib
    -framework Cocoa
    -framework QuartzCore
    -framework CoreVideo
    -framework CoreMedia
    -framework CoreGraphics
    -framework ColorSync
    -framework Metal
    -framework MetalKit
    -framework IOSurface
    -framework VideoToolbox
    -framework AVFoundation
    -framework Network
    -framework Security
    -lpixman-1
    -lxkbcommon
    -lssl
    -lcrypto
    -lz
    target/release/libwawona.a
    -fobjc-arc
    -ObjC
    -Wl,-rpath,/opt/homebrew/lib
    -Wl,-rpath,/opt/homebrew/opt/openssl@3/lib
)

"$CC" "${obj_files[@]}" \
    -o "$OUTPUT_BIN" \
    -isysroot "$SDKROOT" \
    -mmacosx-version-min=14.0 \
    "${link_libs[@]}"

if [ -x "$OUTPUT_BIN" ]; then
    codesign --force --sign - --timestamp=none "$OUTPUT_BIN" 2>/dev/null || true
    echo "Successfully built Wawona macOS compositor and copied to $OUTPUT_BIN"
else
    echo "ERROR: Wawona compositor binary was not created at $OUTPUT_BIN"
    exit 1
fi
