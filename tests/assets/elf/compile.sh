#!/bin/zsh

$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android21-clang \
    -nostdlib \
    -static \
    -Wl,-e,_start \
    tests/assets/elf/test_return_42.c \
    -o build/bin/test_return_42