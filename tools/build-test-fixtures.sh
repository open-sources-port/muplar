#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"

echo "========================================="
echo "Building basic test binary..."
export scriptToRun=$ROOT_DIR/tests/assets/elf/compile-basic.sh
chmod +x ${scriptToRun}
sh ${scriptToRun}

echo "========================================="
echo "Building shared test binary..."
export scriptToRun=$ROOT_DIR/tests/assets/elf/compile-shared-lib.sh
chmod +x ${scriptToRun}
sh ${scriptToRun}

echo "========================================="
echo "Building APK launch-envelope fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-activity-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK dependency fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-dependency-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK JNI-only fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-jni-only-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building Java-only APK classification fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-java-only-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building tiny Java Activity APK fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-tiny-java-activity-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK unsupported import trap fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-unsupported-import-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK asset fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-assets-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK context fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-context-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK binder fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK binder transaction fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-trans-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK local binder fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-local-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK binder string fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-string-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK binder array fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-array-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK binder parcelable fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-parcelable-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK binder lifecycle fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-lifecycle-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK binder driver fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-driver-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK AIDL smoke fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-aidl-smoke-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK generated AIDL coverage fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-aidl-generated-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK binder weak/extension fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-binder-weak-apk.sh
sh ${scriptToRun}

echo "========================================="
echo "Building APK real generated AIDL source fixture..."
export scriptToRun=$ROOT_DIR/tests/assets/apk/create-native-aidl-real-source-apk.sh
sh ${scriptToRun}
