#!/bin/zsh
compiler_android_clang=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android35-clang
readelf_command=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-readelf

mkdir -p build/bin


binary_output=build/bin/test_return_42
$compiler_android_clang -static -Wl,-z,max-page-size=4096 tests/assets/elf/test_return_42.c -o ${binary_output}
file ${binary_output}


# # Should say: ELF 64-bit LSB executable, ARM aarch64
# file ${binary_output}

# # Check ELF type and entry point
# ${readelf_command} -h ${binary_output} | grep -E "Type|Entry|Machine"

# # Check load addresses - should be >= 0x400000 if PIE, or check vaddr
# ${readelf_command} -l ${binary_output} | grep -A1 LOAD

binary_output=build/bin/simple_app_with_print
$compiler_android_clang -static -Wl,-z,max-page-size=4096 tests/assets/elf/simple_app_with_print.c -o ${binary_output}
file ${binary_output}

binary_output=build/bin/test_mt_fork_exec
$compiler_android_clang -static -pthread -Wl,-z,max-page-size=4096 tests/assets/elf/test_mt_fork_exec.c -o ${binary_output}
file ${binary_output}

