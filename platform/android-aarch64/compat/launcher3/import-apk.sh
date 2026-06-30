#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
source "$SCRIPT_DIR/PIN.env"

usage() {
    echo "Usage: $0 APK [OUTPUT_DIR]"
    echo "Imports an AOSP-built pinned Launcher3 APK as a local compatibility fixture."
}

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    usage >&2
    exit 2
fi

INPUT_APK="$1"
OUTPUT_DIR="${2:-$ROOT_DIR/build/launcher3/fixture}"
AAPT2="${AAPT2:-/opt/android/sdk/build-tools/37.0.0/aapt2}"
if [ ! -f "$INPUT_APK" ]; then
    echo "Launcher3 APK not found: $INPUT_APK" >&2
    exit 1
fi
if [ ! -x "$AAPT2" ]; then
    echo "aapt2 not found; set AAPT2" >&2
    exit 1
fi

BADGING="$($AAPT2 dump badging "$INPUT_APK")"
PACKAGE="$(printf '%s\n' "$BADGING" | sed -n "s/^package: name='\([^']*\)'.*/\1/p" | head -1)"
ACTIVITY="$(printf '%s\n' "$BADGING" | sed -n "s/^launchable-activity: name='\([^']*\)'.*/\1/p" | head -1)"
if [ "$PACKAGE" != "$LAUNCHER3_PACKAGE" ]; then
    echo "Expected package $LAUNCHER3_PACKAGE, got ${PACKAGE:-<missing>}" >&2
    exit 1
fi
if [ -z "$ACTIVITY" ]; then
    XMLTREE="$($AAPT2 dump xmltree --file AndroidManifest.xml "$INPUT_APK")"
    ACTIVITY="$(printf '%s\n' "$XMLTREE" | awk '
        function finish() {
            if (!emitted && activity != "" && main && home) {
                print activity; emitted=1; exit
            }
        }
        /^          E: activity / { finish(); activity=""; main=0; home=0; next }
        /^            A: .*:name.*="/ && activity == "" {
            split($0, value, "\""); activity=value[2]; next
        }
        /android.intent.action.MAIN/ { main=1; next }
        /android.intent.category.(HOME|SECONDARY_HOME|LAUNCHER)/ { home=1; next }
        END { if (!emitted) finish() }
    ')"
fi
if [ -z "$ACTIVITY" ]; then
    echo "Launcher3 APK has no MAIN HOME activity" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
cp "$INPUT_APK" "$OUTPUT_DIR/Launcher3.apk"
SHA256="$(shasum -a 256 "$OUTPUT_DIR/Launcher3.apk" | awk '{print $1}')"
PROVIDER="${LAUNCHER3_ARTIFACT_PROVIDER:-aosp-source-build}"
PROVIDER_REF="${LAUNCHER3_ARTIFACT_REF:-$LAUNCHER3_TAG}"
PROVIDER_CHECKSUM="${LAUNCHER3_ARTIFACT_CHECKSUM:-$LAUNCHER3_COMMIT}"
cat > "$OUTPUT_DIR/provenance.properties" <<EOF
repository=$LAUNCHER3_REPOSITORY
tag=$LAUNCHER3_TAG
commit=$LAUNCHER3_COMMIT
module=$LAUNCHER3_MODULE
artifact_provider=$PROVIDER
artifact_ref=$PROVIDER_REF
artifact_provider_checksum=$PROVIDER_CHECKSUM
package=$PACKAGE
activity=$ACTIVITY
sha256=$SHA256
source_apk=$(cd "$(dirname "$INPUT_APK")" && pwd)/$(basename "$INPUT_APK")
EOF

echo "[Launcher3] fixture: $OUTPUT_DIR/Launcher3.apk"
echo "[Launcher3] activity: $ACTIVITY"
echo "[Launcher3] sha256: $SHA256"
