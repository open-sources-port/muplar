#!/bin/zsh
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
MUPLAR_WAYLAND_SRC="${ROOT_DIR}/third_party/muplar-wayland"
WAWONA_SRC="${MUPLAR_WAYLAND_SRC}"
BUILD_DIR="${ROOT_DIR}/build"

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
    bash "${MUPLAR_WAYLAND_SRC}/dependencies/libs/waypipe/patch-waypipe-source.sh"
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

# Run cargo build --release. Cargo's binary is a platform stub; the macOS
# compositor entrypoint is Objective-C and links against the Rust staticlib.
"$CARGO_BIN" build --release

WAWONA_RUST_STATICLIB="target/release/libmuplar_wayland.a"
if [ ! -f "$WAWONA_RUST_STATICLIB" ]; then
    echo "ERROR: Muplar Wayland static library not found at $WAWONA_RUST_STATICLIB"
    exit 1
fi

echo "Successfully built Muplar Wayland static library."
exit 0
