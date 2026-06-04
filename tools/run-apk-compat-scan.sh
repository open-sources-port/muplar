#!/bin/zsh

set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT_NAME="$0"
ANGLE_DYLIB_DIR="$ROOT_DIR/third_party/angle-bin"
if [ -d "$ANGLE_DYLIB_DIR" ]; then
    export DYLD_LIBRARY_PATH="$ANGLE_DYLIB_DIR${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
fi
MUP="$ROOT_DIR/build/bin/mup"
SYSROOT="$ROOT_DIR/build/sysroot"
PREFIX=""
LOG_DIR="$ROOT_DIR/build/apk-compat-scan"
REPORT="$LOG_DIR/report.md"
REPORT_EXPLICIT=false
APK_LIB_NAME=""
MUP_ENTRY_ARGS=()
HV_RETRIES="${MUPLAR_HV_RETRIES:-2}"
HV_RETRY_SLEEP="${MUPLAR_HV_RETRY_SLEEP:-5}"
APKS=()

guess_jni_call_parts() {
    local jni_symbol="$1"
    jni_symbol="${jni_symbol%%@@*}"
    jni_symbol="${jni_symbol%%@*}"
    local body="${jni_symbol#Java_}"
    local main="${body%%__*}"
    local class_part="${main%_*}"
    local method_part="${main##*_}"

    if [ "$class_part" = "$main" ] || [ -z "$class_part" ] ||
       [ -z "$method_part" ]; then
        printf "\t\n"
        return
    fi

    local class_guess="${class_part//_//}"
    local method_guess="$method_part"
    method_guess="${method_guess//_1/_}"
    method_guess="${method_guess//_2/;}"
    method_guess="${method_guess//_3/[}"
    printf "%s\t%s\n" "$class_guess" "$method_guess"
}

usage() {
    echo "Usage: $SCRIPT_NAME [--mup PATH] [--sysroot PATH] [--prefix NAME|PATH] [--log-dir PATH] [--report PATH] [--apk-lib NAME] [--jni-call CLASS METHOD SIGNATURE] [--jni-static|--jni-instance] [--jni-int VALUE ...] [--hv-retries N] [--hv-retry-sleep SECONDS] APK..."
    echo
    echo "Runs each APK with --strict-direct-imports and summarizes native"
    echo "dependency/import gaps before broader app startup debugging."
    echo
    echo "--apk-lib selects one native library when an APK has multiple arm64"
    echo "libraries and no NativeActivity lib_name. Use it when scanning that APK"
    echo "by itself."
    echo
    echo "--jni-call and --jni-int are passed through to mup for JNI-only APK"
    echo "libraries that have exported native methods but no default entrypoint."
    echo
    echo "--hv-retries and --hv-retry-sleep handle transient macOS"
    echo "Hypervisor.framework hv_vm_create failures during large scans."
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
        --prefix)
            if [ "$#" -lt 2 ]; then
                echo "--prefix requires a name or path" >&2
                exit 2
            fi
            PREFIX="$2"
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
        --apk-lib)
            if [ "$#" -lt 2 ]; then
                echo "--apk-lib requires a library name" >&2
                exit 2
            fi
            APK_LIB_NAME="$2"
            shift 2
            ;;
        --jni-call)
            if [ "$#" -lt 4 ]; then
                echo "--jni-call requires CLASS METHOD SIGNATURE" >&2
                exit 2
            fi
            MUP_ENTRY_ARGS+=(--jni-call "$2" "$3" "$4")
            shift 4
            ;;
        --jni-static|--jni-instance)
            MUP_ENTRY_ARGS+=("$1")
            shift
            ;;
        --jni-int|--jni-arg)
            if [ "$#" -lt 2 ]; then
                echo "$1 requires a value" >&2
                exit 2
            fi
            MUP_ENTRY_ARGS+=("$1" "$2")
            shift 2
            ;;
        --hv-retries)
            if [ "$#" -lt 2 ]; then
                echo "--hv-retries requires a number" >&2
                exit 2
            fi
            HV_RETRIES="$2"
            shift 2
            ;;
        --hv-retry-sleep)
            if [ "$#" -lt 2 ]; then
                echo "--hv-retry-sleep requires seconds" >&2
                exit 2
            fi
            HV_RETRY_SLEEP="$2"
            shift 2
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

if ! [[ "$HV_RETRIES" == <-> ]]; then
    echo "--hv-retries must be a non-negative integer" >&2
    exit 2
fi

if ! [[ "$HV_RETRY_SLEEP" == <-> ]]; then
    echo "--hv-retry-sleep must be a non-negative integer" >&2
    exit 2
fi

LLVM_NM=""
if [ -n "${ANDROID_NDK_HOME:-}" ] &&
   [ -x "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-nm" ]; then
    LLVM_NM="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-nm"
elif [ -x "/opt/homebrew/share/android-ndk/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-nm" ]; then
    LLVM_NM="/opt/homebrew/share/android-ndk/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-nm"
elif command -v llvm-nm >/dev/null 2>&1; then
    LLVM_NM="$(command -v llvm-nm)"
fi

PYTHON3=""
DEX_SIG_TOOL="$ROOT_DIR/tools/dex-method-signatures.py"
if command -v python3 >/dev/null 2>&1; then
    PYTHON3="$(command -v python3)"
fi

mkdir -p "$LOG_DIR"
mkdir -p "$(dirname "$REPORT")"

SUMMARY_TMP="$LOG_DIR/.compat-summary-$$.tsv"
MISSING_TMP="$LOG_DIR/.compat-missing-$$.tsv"
UNRESOLVED_TMP="$LOG_DIR/.compat-unresolved-$$.tsv"
CAPS_TMP="$LOG_DIR/.compat-capped-$$.tsv"
LIBSEL_TMP="$LOG_DIR/.compat-lib-selection-$$.tsv"
ENTRY_TMP="$LOG_DIR/.compat-entrypoint-$$.tsv"
JNI_EXPORT_TMP="$LOG_DIR/.compat-jni-exports-$$.tsv"
JAVA_TMP="$LOG_DIR/.compat-java-runtime-$$.tsv"
trap 'rm -f "$SUMMARY_TMP" "$MISSING_TMP" "$UNRESOLVED_TMP" "$CAPS_TMP" "$LIBSEL_TMP" "$ENTRY_TMP" "$JNI_EXPORT_TMP" "$JAVA_TMP"' EXIT

: > "$SUMMARY_TMP"
: > "$MISSING_TMP"
: > "$UNRESOLVED_TMP"
: > "$CAPS_TMP"
: > "$LIBSEL_TMP"
: > "$ENTRY_TMP"
: > "$JNI_EXPORT_TMP"
: > "$JAVA_TMP"

run_mup_with_hv_retry() {
    local log="$1"
    shift

    local attempt=0
    local rc=0
    while true; do
        "$@" > "$log" 2>&1
        rc=$?
        if ! grep -q "hv_vm_create failed" "$log"; then
            return "$rc"
        fi
        if [ "$attempt" -ge "$HV_RETRIES" ]; then
            return "$rc"
        fi

        attempt=$((attempt + 1))
        echo "host HV unavailable; retrying $attempt/$HV_RETRIES after ${HV_RETRY_SLEEP}s" >&2
        sleep "$HV_RETRY_SLEEP"
    done
}

overall=0
for apk in "${APKS[@]}"; do
    if [ ! -f "$apk" ]; then
        echo "APK: $apk"
        echo "status: missing-file"
        echo
        printf "missing-file\t-\t%s\t-\n" "$apk" >> "$SUMMARY_TMP"
        overall=1
        continue
    fi

    base="${apk:t}"
    slug="$(printf "%s" "$base" | tr -c 'A-Za-z0-9._-' '_')"
    sum="$(cksum < "$apk" | awk '{print $1}')"
    log="$LOG_DIR/${slug:r}-$sum.log"

    cmd=("$MUP" --strict-direct-imports --sysroot "$SYSROOT")
    if [ -n "$PREFIX" ]; then
        cmd+=(--prefix "$PREFIX")
    fi
    if [ -n "$APK_LIB_NAME" ]; then
        cmd+=(--apk-lib "$APK_LIB_NAME")
    fi
    cmd+=("${MUP_ENTRY_ARGS[@]}")
    cmd+=("$apk")
    run_mup_with_hv_retry "$log" "${cmd[@]}"
    rc=$?

    lib_selection_required="$(
        sed -n 's/^APK error: APK has multiple arm64 native libraries and no clear NativeActivity lib_name; use --apk-lib\. Available: //p' "$log" |
        sort -u
    )"
    entrypoint_required="$(
        sed -n 's/^.*no NativeActivity entry found in //p' "$log" |
        sort -u
    )"
    java_runtime_required="$(
        sed -n \
            -e 's/^APK error: APK runtime kind is java-only; Java\/ART APK launch is not implemented yet.*/Java\/ART APK launch is not implemented yet/p' \
            -e 's/^APK error: Java\/ART bootstrap incomplete: missing /Missing Java\/ART bootstrap inputs: /p' \
            -e 's/^APK error: Java\/ART bootstrap plan is ready.*/app_process64 execution is not implemented yet/p' \
            "$log" |
        sort -u
    )"
    missing="$(
        sed -n 's/^.*required direct \.so dependency not found locally: //p' "$log" |
        sort -u
    )"
    unresolved="$(
        sed -n 's/^.*unresolved direct \.so import: \(.*\) needs \(.*\) (reloc=.*/\1 needs \2/p' "$log" |
        sort -u
    )"
    capped="$(
        sed -n 's/^.*unresolved direct \.so import: \(.*\) has more unresolved imports.*/\1 has more unresolved imports/p' "$log" |
        sort -u
    )"

    if [ "$rc" -eq 0 ]; then
        scan_status="launch-ok"
    elif [ -n "$lib_selection_required" ]; then
        scan_status="apk-lib-required"
    elif [ -n "$java_runtime_required" ]; then
        scan_status="java-runtime-required"
    elif [ -n "$entrypoint_required" ]; then
        scan_status="entrypoint-required"
    elif grep -q "hv_vm_create failed" "$log"; then
        scan_status="host-hv-unavailable"
    elif [ -n "$missing$unresolved$capped" ] || grep -q "strict direct import mode" "$log"; then
        scan_status="native-deps-incomplete"
    else
        scan_status="runtime-failed"
    fi

    echo "APK: $apk"
    echo "status: $scan_status (rc=$rc)"
    echo "log: $log"
    printf "%s\t%s\t%s\t%s\n" "$scan_status" "$rc" "$apk" "$log" >> "$SUMMARY_TMP"

    if [ -n "$lib_selection_required" ]; then
        echo "available APK libs:"
        echo "$lib_selection_required" | sed 's/^/  /'
        while IFS= read -r available; do
            [ -n "$available" ] && printf "%s\t%s\n" "$available" "$apk" >> "$LIBSEL_TMP"
        done <<< "$lib_selection_required"
    fi

    if [ "$scan_status" = "host-hv-unavailable" ]; then
        echo "host HV unavailable:"
        echo "  hv_vm_create failed after $((HV_RETRIES + 1)) attempt(s); rerun later or increase --hv-retries"
    fi

    if [ -n "$java_runtime_required" ]; then
        echo "java runtime required:"
        echo "$java_runtime_required" | sed 's/^/  /'
        printf "%s\t%s\n" "$java_runtime_required" "$apk" >> "$JAVA_TMP"
    fi

    if [ -n "$entrypoint_required" ]; then
        echo "entrypoint required:"
        echo "  no JNI_OnLoad or NativeActivity entry; rerun with --jni-call if this is a JNI-only library"
        while IFS= read -r selected_path; do
            [ -n "$selected_path" ] || continue
            printf "%s\t%s\n" "$selected_path" "$apk" >> "$ENTRY_TMP"

            if [ -n "$LLVM_NM" ] && [ -f "$selected_path" ]; then
                jni_exports="$(
                    "$LLVM_NM" -D --defined-only "$selected_path" 2>/dev/null |
                    awk '{print $NF}' |
                    grep '^Java_' |
                    sort -u
                )"
                if [ -n "$jni_exports" ]; then
                    echo "exported JNI methods:"
                    while IFS= read -r jni_symbol; do
                        [ -n "$jni_symbol" ] || continue
                        parts="$(guess_jni_call_parts "$jni_symbol")"
                        IFS=$'\t' read -r class_guess method_guess <<< "$parts"
                        signatures=""
                        if [ -n "$PYTHON3" ] && [ -x "$DEX_SIG_TOOL" ] &&
                           [ -n "$class_guess" ] && [ -n "$method_guess" ]; then
                            signatures="$(
                                "$PYTHON3" "$DEX_SIG_TOOL" --with-flags "$apk" \
                                    "$class_guess" "$method_guess" 2>/dev/null ||
                                    true
                            )"
                        fi

                        if [ -n "$signatures" ]; then
                            while IFS=$'\t' read -r signature receiver_kind; do
                                [ -n "$signature" ] || continue
                                receiver_flag=""
                                if [ "$receiver_kind" = "static" ]; then
                                    receiver_flag=" --jni-static"
                                elif [ "$receiver_kind" = "instance" ]; then
                                    receiver_flag=" --jni-instance"
                                fi
                                echo "  $jni_symbol -> --jni-call $class_guess $method_guess '$signature'$receiver_flag"
                                printf "%s\t%s\t%s\t%s\t%s\n" \
                                    "$jni_symbol" "$class_guess" \
                                    "$method_guess" "$signature" "$apk" \
                                    >> "$JNI_EXPORT_TMP"
                            done <<< "$signatures"
                        elif [ -n "$class_guess" ] && [ -n "$method_guess" ]; then
                            echo "  $jni_symbol -> --jni-call $class_guess $method_guess SIGNATURE"
                            printf "%s\t%s\t%s\t\t%s\n" \
                                "$jni_symbol" "$class_guess" "$method_guess" "$apk" \
                                >> "$JNI_EXPORT_TMP"
                        else
                            echo "  $jni_symbol"
                            printf "%s\t\t\t\t%s\n" "$jni_symbol" "$apk" \
                                >> "$JNI_EXPORT_TMP"
                        fi
                    done <<< "$jni_exports"
                fi
            fi
        done <<< "$entrypoint_required"
    fi

    if [ -n "$missing" ]; then
        echo "missing DT_NEEDED:"
        echo "$missing" | sed 's/^/  /'
        while IFS= read -r dep; do
            [ -n "$dep" ] && printf "%s\t%s\n" "$dep" "$apk" >> "$MISSING_TMP"
        done <<< "$missing"
    fi

    if [ -n "$unresolved" ]; then
        echo "unresolved imports:"
        echo "$unresolved" | sed 's/^/  /'
        while IFS= read -r import; do
            [ -n "$import" ] && printf "%s\t%s\n" "$import" "$apk" >> "$UNRESOLVED_TMP"
        done <<< "$unresolved"
    fi

    if [ -n "$capped" ]; then
        echo "unresolved imports:"
        echo "$capped" | sed 's/^/  /'
        while IFS= read -r capped_obj; do
            [ -n "$capped_obj" ] && printf "%s\t%s\n" "$capped_obj" "$apk" >> "$CAPS_TMP"
        done <<< "$capped"
    fi

    echo

    if [ "$scan_status" != "launch-ok" ]; then
        overall=1
    fi
done

{
    printf "# Muplar APK Compatibility Scan\n\n"
    printf 'Strict native import mode: `mup --strict-direct-imports`\n\n'

    printf "## APK Results\n\n"
    printf "| Status | RC | APK | Log |\n"
    printf "|---|---:|---|---|\n"
    while IFS=$'\t' read -r scan_status rc apk log; do
        printf '| %s | %s | `%s` | `%s` |\n' "$scan_status" "$rc" "$apk" "$log"
    done < "$SUMMARY_TMP"

    printf "\n## Stub Backlog\n\n"
    if [ -s "$MISSING_TMP" ] || [ -s "$UNRESOLVED_TMP" ] ||
       [ -s "$CAPS_TMP" ] || [ -s "$LIBSEL_TMP" ] ||
       [ -s "$ENTRY_TMP" ] || [ -s "$JNI_EXPORT_TMP" ] ||
       [ -s "$JAVA_TMP" ]; then
        if [ -s "$JAVA_TMP" ]; then
            printf "### Java/ART Runtime Required\n\n"
            sort -u "$JAVA_TMP" |
            while IFS=$'\t' read -r reason apk; do
                printf -- '- `%s` contains DEX bytecode and needs the Java/ART APK launch path before it can run normally. `%s`.\n' "$apk" "$reason"
            done
            printf "\n"
        fi

        if [ -s "$LIBSEL_TMP" ]; then
            printf "### APK Library Selection Required\n\n"
            sort -u "$LIBSEL_TMP" |
            while IFS=$'\t' read -r available apk; do
                printf -- '- `%s` has multiple native libraries; rerun that APK with `--apk-lib NAME`. Available: `%s`\n' "$apk" "$available"
            done
            printf "\n"
        fi

        if [ -s "$ENTRY_TMP" ]; then
            printf "### Explicit Entrypoint Required\n\n"
            sort -u "$ENTRY_TMP" |
            while IFS=$'\t' read -r selected_path apk; do
                printf -- '- `%s` selected `%s`, which has no `JNI_OnLoad` or NativeActivity entry. Rerun with `--jni-call CLASS METHOD SIGNATURE` for exported JNI methods.\n' "$apk" "$selected_path"
            done
            printf "\n"
        fi

        if [ -s "$JNI_EXPORT_TMP" ]; then
            printf "### Exported JNI Methods\n\n"
            sort -u "$JNI_EXPORT_TMP" |
            while IFS=$'\t' read -r jni_symbol class_guess method_guess signature apk; do
                if [ -n "$signature" ]; then
                    printf -- "- \`%s\` exports \`%s\`; try \`--jni-call %s %s '%s'\`.\n" "$apk" "$jni_symbol" "$class_guess" "$method_guess" "$signature"
                elif [ -n "$class_guess" ] && [ -n "$method_guess" ]; then
                    printf -- '- `%s` exports `%s`; try `--jni-call %s %s SIGNATURE` with the Java method signature.\n' "$apk" "$jni_symbol" "$class_guess" "$method_guess"
                else
                    printf -- '- `%s` exports `%s`.\n' "$apk" "$jni_symbol"
                fi
            done
            printf "\n"
        fi

        if [ -s "$MISSING_TMP" ]; then
            printf "### Missing APK-Local Libraries\n\n"
            sort -u "$MISSING_TMP" |
            while IFS=$'\t' read -r dep apk; do
                printf -- '- `%s` required by `%s`\n' "$dep" "$apk"
            done
            printf "\n"
        fi

        if [ -s "$UNRESOLVED_TMP" ]; then
            printf "### Unresolved Direct Imports\n\n"
            sort -u "$UNRESOLVED_TMP" |
            while IFS=$'\t' read -r import apk; do
                printf -- '- `%s` seen in `%s`\n' "$import" "$apk"
            done
            printf "\n"
        fi

        if [ -s "$CAPS_TMP" ]; then
            printf "### Capped Import Lists\n\n"
            sort -u "$CAPS_TMP" |
            while IFS=$'\t' read -r capped_obj apk; do
                printf -- '- `%s` in `%s`; rerun directly for full detail\n' "$capped_obj" "$apk"
            done
            printf "\n"
        fi
    else
        printf "No missing APK-local libraries or unresolved direct imports found.\n\n"
    fi

    printf "## Next Actions\n\n"
    printf '%s\n' '- For `java-runtime-required`, import an ART-capable Android sysroot with `tools/import-android-art-sysroot.sh`, then verify it with `tools/check-android-art-sysroot.sh`.'
    printf '%s\n' "- Add focused runtime stubs for unresolved imports that appear in real APK startup paths."
    printf '%s\n' '- Add APK-local dependencies when a required `DT_NEEDED` library is missing from the package scan root.'
    printf '%s\n' "- Rerun this scan after each stub/dependency change to keep the backlog shrinking."
} > "$REPORT"

echo "report: $REPORT"
exit "$overall"
