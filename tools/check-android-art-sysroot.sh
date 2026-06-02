#!/bin/zsh

set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT_NAME="$0"
SYSROOT="$ROOT_DIR/build/sysroot"
QUIET=false

usage() {
    echo "Usage: $SCRIPT_NAME [--sysroot PATH] [--quiet]"
    echo
    echo "Checks whether a Muplar Android sysroot has the minimum ART"
    echo "bootstrap files needed before Java APK startup can run."
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --sysroot)
            if [ "$#" -lt 2 ]; then
                echo "--sysroot requires a path" >&2
                exit 2
            fi
            SYSROOT="$2"
            shift 2
            ;;
        --quiet)
            QUIET=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [ ! -d "$SYSROOT" ]; then
    echo "sysroot not found: $SYSROOT" >&2
    exit 2
fi

missing=0

check_any_file() {
    local label="$1"
    local required="$2"
    shift 2

    local found=""
    local rel
    for rel in "$@"; do
        if [ -f "$SYSROOT/$rel" ]; then
            found="$rel"
            break
        fi
    done

    if [ -n "$found" ]; then
        if [ "$QUIET" = false ]; then
            echo "  [ok] $label: $found"
        fi
        return
    fi

    if [ "$required" = true ]; then
        missing=$((missing + 1))
        if [ "$QUIET" = false ]; then
            echo "  [missing] $label"
            printf "            tried:"
            for rel in "$@"; do
                printf " %s" "$rel"
            done
            printf "\n"
        fi
    elif [ "$QUIET" = false ]; then
        echo "  [optional-missing] $label"
    fi
}

if [ "$QUIET" = false ]; then
    echo "Android ART sysroot check: $SYSROOT"
    echo "Required:"
fi

check_any_file "app_process64" true \
    system/bin/app_process64 \
    apex/com.android.art/bin/app_process64
check_any_file "core-oj.jar" true \
    apex/com.android.art/javalib/core-oj.jar \
    system/framework/core-oj.jar
check_any_file "core-libart.jar" true \
    apex/com.android.art/javalib/core-libart.jar \
    system/framework/core-libart.jar
check_any_file "framework.jar" true \
    system/framework/framework.jar
check_any_file "libandroid_runtime.so" true \
    system/lib64/libandroid_runtime.so
check_any_file "libart.so" true \
    apex/com.android.art/lib64/libart.so \
    system/lib64/libart.so

if [ "$QUIET" = false ]; then
    echo "Optional:"
fi

check_any_file "conscrypt.jar" false \
    apex/com.android.conscrypt/javalib/conscrypt.jar \
    system/framework/conscrypt.jar
check_any_file "core-icu4j.jar" false \
    apex/com.android.i18n/javalib/core-icu4j.jar
check_any_file "okhttp.jar" false \
    apex/com.android.art/javalib/okhttp.jar \
    system/framework/okhttp.jar
check_any_file "bouncycastle.jar" false \
    apex/com.android.art/javalib/bouncycastle.jar \
    system/framework/bouncycastle.jar
check_any_file "apache-xml.jar" false \
    apex/com.android.art/javalib/apache-xml.jar \
    system/framework/apache-xml.jar

if [ "$missing" -eq 0 ]; then
    if [ "$QUIET" = false ]; then
        echo "status: art-sysroot-ready"
    fi
    exit 0
fi

if [ "$QUIET" = false ]; then
    echo "status: art-sysroot-incomplete"
fi
exit 1
