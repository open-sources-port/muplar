/* echo arguments
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: argv passing, printf formatting.
 */

#include <stdio.h>

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            putchar(' ');
        fputs(argv[i], stdout);
    }
    putchar('\n');
    return 0;
}
