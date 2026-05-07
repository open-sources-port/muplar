/* verify argc/argv correctness
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: Linux stack layout (argc at SP, argv pointers, strings).
 */

#include <stdio.h>

int main(int argc, char *argv[])
{
    printf("argc=%d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("argv[%d]=\"%s\"\n", i, argv[i]);
    }
    return 0;
}
