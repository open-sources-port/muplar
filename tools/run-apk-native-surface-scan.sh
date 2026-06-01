#!/bin/zsh

set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT_NAME="$0"
MUP="$ROOT_DIR/build/bin/mup"
SYSROOT="$ROOT_DIR/build/sysroot"
LOG_DIR="$ROOT_DIR/build/apk-native-surface-scan"
REPORT="$LOG_DIR/report.md"
REPORT_EXPLICIT=false
MAX_JNI_METHODS=3
PROBE_JNI=true
APKS=()

usage() {
    echo "Usage: $SCRIPT_NAME [--mup PATH] [--sysroot PATH] [--log-dir PATH] [--report PATH] [--max-jni-methods N] [--no-jni-probe] APK..."
    echo
    echo "Enumerates each APK's arm64 native libraries, runs each selected library"
    echo "through the compatibility scanner, and probes exported JNI methods when"
    echo "a library has no JNI_OnLoad or NativeActivity entrypoint."
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --mup)
            if [ "$#" -lt 2 ]; then
                echo "--mup requires a path" >&2
                exit 2
            fi
            MUP="$2"
            shift 2
            ;;
        --sysroot)
            if [ "$#" -lt 2 ]; then
                echo "--sysroot requires a path" >&2
                exit 2
            fi
            SYSROOT="$2"
            shift 2
            ;;
        --log-dir)
            if [ "$#" -lt 2 ]; then
                echo "--log-dir requires a path" >&2
                exit 2
            fi
            LOG_DIR="$2"
            if [ "$REPORT_EXPLICIT" = false ]; then
                REPORT="$LOG_DIR/report.md"
            fi
            shift 2
            ;;
        --report)
            if [ "$#" -lt 2 ]; then
                echo "--report requires a path" >&2
                exit 2
            fi
            REPORT="$2"
            REPORT_EXPLICIT=true
            shift 2
            ;;
        --max-jni-methods)
            if [ "$#" -lt 2 ]; then
                echo "--max-jni-methods requires a number" >&2
                exit 2
            fi
            MAX_JNI_METHODS="$2"
            shift 2
            ;;
        --no-jni-probe)
            PROBE_JNI=false
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            while [ "$#" -gt 0 ]; do
                APKS+=("$1")
                shift
            done
            ;;
        -*)
            echo "Unknown flag: $1" >&2
            usage >&2
            exit 2
            ;;
        *)
            APKS+=("$1")
            shift
            ;;
    esac
done

if [ "${#APKS[@]}" -eq 0 ]; then
    usage >&2
    exit 2
fi

if [ ! -x "$MUP" ]; then
    echo "mup binary not found or not executable: $MUP" >&2
    exit 2
fi

if [ ! -d "$SYSROOT" ]; then
    echo "sysroot not found: $SYSROOT" >&2
    exit 2
fi

if ! [[ "$MAX_JNI_METHODS" == <-> ]]; then
    echo "--max-jni-methods must be a non-negative integer" >&2
    exit 2
fi

COMPAT_SCAN="$ROOT_DIR/tools/run-apk-compat-scan.sh"
if [ ! -x "$COMPAT_SCAN" ]; then
    echo "compat scanner not found or not executable: $COMPAT_SCAN" >&2
    exit 2
fi

mkdir -p "$LOG_DIR"
mkdir -p "$(dirname "$REPORT")"

LIB_TMP="$LOG_DIR/.surface-libs-$$.tsv"
JNI_TMP="$LOG_DIR/.surface-jni-$$.tsv"
trap 'rm -f "$LIB_TMP" "$JNI_TMP"' EXIT
: > "$LIB_TMP"
: > "$JNI_TMP"

slug_for() {
    printf "%s" "$1" | tr -c 'A-Za-z0-9._-' '_'
}

apk_libs() {
    unzip -Z1 "$1" 2>/dev/null |
        sed -n 's#^lib/arm64-v8a/lib\(.*\)\.so$#\1#p' |
        sort -u
}

jni_arg_count() {
    local sig="$1"
    local params="${sig#\(}"
    params="${params%%\)*}"
    local count=0
    local c=""

    while [ -n "$params" ]; do
        c="${params[1,1]}"
        if [ "$c" = "[" ]; then
            while [ "${params[1,1]}" = "[" ]; do
                params="${params[2,-1]}"
            done
            if [ "${params[1,1]}" = "L" ]; then
                params="${params#*;}"
            else
                params="${params[2,-1]}"
            fi
            count=$((count + 1))
        elif [ "$c" = "L" ]; then
            params="${params#*;}"
            count=$((count + 1))
        else
            params="${params[2,-1]}"
            count=$((count + 1))
        fi
    done

    echo "$count"
}

parse_status() {
    sed -n 's/^status: \([^ ]*\) (rc=\([0-9-]*\)).*/\1\t\2/p' "$1" |
        tail -n 1
}

overall=0
for apk in "${APKS[@]}"; do
    echo "APK: $apk"
    if [ ! -f "$apk" ]; then
        echo "status: missing-file"
        printf "missing-file\t-\t%s\t-\t-\t-\n" "$apk" >> "$LIB_TMP"
        overall=1
        echo
        continue
    fi

    libs="$(apk_libs "$apk")"
    if [ -z "$libs" ]; then
        echo "status: no-arm64-native-libs"
        printf "no-arm64-native-libs\t-\t%s\t-\t-\t-\n" "$apk" >> "$LIB_TMP"
        echo
        continue
    fi

    apk_slug="$(slug_for "${apk:t}")"
    while IFS= read -r lib_name; do
        [ -n "$lib_name" ] || continue

        lib_slug="$(slug_for "$lib_name")"
        scan_log="$LOG_DIR/${apk_slug:r}-${lib_slug}.compat.txt"
        scan_report="$LOG_DIR/${apk_slug:r}-${lib_slug}.compat.md"
        compat_log_dir="$LOG_DIR/compat"

        "$COMPAT_SCAN" \
            --mup "$MUP" \
            --sysroot "$SYSROOT" \
            --log-dir "$compat_log_dir" \
            --report "$scan_report" \
            --apk-lib "$lib_name" \
            "$apk" > "$scan_log" 2>&1
        scan_rc=$?

        parsed="$(parse_status "$scan_log")"
        lib_status="${parsed%%	*}"
        lib_rc="${parsed#*	}"
        [ -n "$lib_status" ] || lib_status="scan-failed"
        [ -n "$lib_rc" ] || lib_rc="$scan_rc"

        echo "  lib/$lib_name.so: $lib_status (rc=$lib_rc)"
        printf "%s\t%s\t%s\t%s\t%s\t%s\n" \
            "$lib_status" "$lib_rc" "$apk" "$lib_name" "$scan_log" "$scan_report" \
            >> "$LIB_TMP"

        if [ "$lib_status" = "runtime-failed" ] ||
           [ "$lib_status" = "native-deps-incomplete" ] ||
           [ "$lib_status" = "scan-failed" ]; then
            overall=1
        fi

        if [ "$PROBE_JNI" = true ] &&
           [ "$MAX_JNI_METHODS" -gt 0 ] &&
           [ "$lib_status" = "entrypoint-required" ]; then
            candidates="$(
                sed -n "s/^  .* -> --jni-call \([^ ]*\) \([^ ]*\) '\([^']*\)'$/\1\t\2\t\3/p" "$scan_log"
            )"
            if [ -z "$candidates" ]; then
                printf "not-probed\t-\t%s\t%s\t-\t-\t-\t-\t-\t%s\n" \
                    "$apk" "$lib_name" "$scan_log" >> "$JNI_TMP"
                continue
            fi

            probed=0
            while IFS=$'\t' read -r class_name method_name signature; do
                [ -n "$class_name" ] || continue
                [ -n "$signature" ] || continue
                if [ "$probed" -ge "$MAX_JNI_METHODS" ]; then
                    break
                fi

                arg_count="$(jni_arg_count "$signature")"
                if [ "$arg_count" -gt 6 ]; then
                    echo "    $class_name.$method_name$signature: skipped ($arg_count args)"
                    printf "skipped-too-many-args\t-\t%s\t%s\t%s\t%s\t%s\t%s\t-\t%s\n" \
                        "$apk" "$lib_name" "$class_name" "$method_name" \
                        "$signature" "$arg_count" "$scan_log" >> "$JNI_TMP"
                    continue
                fi

                probed=$((probed + 1))
                method_log="$LOG_DIR/${apk_slug:r}-${lib_slug}-jni-${probed}.txt"
                method_report="$LOG_DIR/${apk_slug:r}-${lib_slug}-jni-${probed}.md"
                "$COMPAT_SCAN" \
                    --mup "$MUP" \
                    --sysroot "$SYSROOT" \
                    --log-dir "$compat_log_dir" \
                    --report "$method_report" \
                    --apk-lib "$lib_name" \
                    --jni-call "$class_name" "$method_name" "$signature" \
                    "$apk" > "$method_log" 2>&1
                method_rc=$?

                method_parsed="$(parse_status "$method_log")"
                method_status="${method_parsed%%	*}"
                method_status="${method_status:-scan-failed}"
                method_rc_parsed="${method_parsed#*	}"
                [ -n "$method_rc_parsed" ] || method_rc_parsed="$method_rc"

                echo "    $class_name.$method_name$signature: $method_status (rc=$method_rc_parsed)"
                printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
                    "$method_status" "$method_rc_parsed" "$apk" "$lib_name" \
                    "$class_name" "$method_name" "$signature" "$arg_count" \
                    "$method_log" "$method_report" >> "$JNI_TMP"

                if [ "$method_status" != "launch-ok" ]; then
                    overall=1
                fi
            done <<< "$candidates"
        fi
    done <<< "$libs"
    echo
done

{
    printf "# Muplar APK Native Surface Scan\n\n"

    printf "## Library Results\n\n"
    printf "| Status | RC | APK | Library | Log |\n"
    printf "|---|---:|---|---|---|\n"
    while IFS=$'\t' read -r row_status rc apk lib_name log report; do
        printf '| %s | %s | `%s` | `%s` | `%s` |\n' \
            "$row_status" "$rc" "$apk" "$lib_name" "$log"
    done < "$LIB_TMP"

    printf "\n## JNI Method Probes\n\n"
    if [ -s "$JNI_TMP" ]; then
        printf "| Status | RC | APK | Library | Method | Signature | Args | Log |\n"
        printf "|---|---:|---|---|---|---|---:|---|\n"
        while IFS=$'\t' read -r row_status rc apk lib_name class_name method_name signature arg_count log report; do
            printf '| %s | %s | `%s` | `%s` | `%s.%s` | `%s` | %s | `%s` |\n' \
                "$row_status" "$rc" "$apk" "$lib_name" "$class_name" \
                "$method_name" "$signature" "$arg_count" "$log"
        done < "$JNI_TMP"
        printf "\n"
    else
        printf "No JNI-only methods were probed.\n\n"
    fi

    printf "## Next Actions\n\n"
    printf '%s\n' "- For libraries marked \`native-deps-incomplete\`, inspect their compatibility logs for missing platform dependencies or direct imports."
    printf '%s\n' "- For JNI probes marked \`runtime-failed\`, inspect the method log; those are now real method-level failures instead of entrypoint selection gaps."
    printf '%s\n' "- Raise \`--max-jni-methods\` when you want broader JNI coverage for large libraries."
} > "$REPORT"

echo "report: $REPORT"
exit "$overall"
