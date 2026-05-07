/* file write+read roundtrip test
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests: fopen(w), fwrite, fclose, fopen(r), fread, fclose, remove.
 * Verifies data integrity through a complete write->read cycle.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *path = "/tmp/elfuse-roundtrip-test.bin";
    const int N = 1000;

    /* Generate test data: array of squares */
    int *data = malloc(N * sizeof(int));
    if (!data) {
        perror("malloc");
        return 1;
    }
    for (int i = 0; i < N; i++)
        data[i] = i * i;

    /* Write */
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        perror("fopen(w)");
        return 1;
    }
    size_t nw = fwrite(data, sizeof(int), N, fp);
    fclose(fp);
    if (nw != (size_t) N) {
        printf("FAIL: fwrite wrote %zu of %d ints\n", nw, N);
        free(data);
        remove(path);
        return 1;
    }
    printf("Wrote %zu ints\n", nw);

    /* Read back */
    int *buf = calloc(N, sizeof(int));
    if (!buf) {
        perror("calloc");
        free(data);
        remove(path);
        return 1;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        perror("fopen(r)");
        free(data);
        free(buf);
        remove(path);
        return 1;
    }
    size_t nr = fread(buf, sizeof(int), N, fp);
    fclose(fp);
    printf("Read %zu ints\n", nr);

    /* Verify */
    bool ok = true;
    for (int i = 0; i < N; i++) {
        if (buf[i] != i * i) {
            printf("MISMATCH at [%d]: expected %d, got %d\n", i, i * i, buf[i]);
            ok = false;
            break;
        }
    }

    if (ok)
        printf("Data verified OK\n");

    free(data);
    free(buf);
    remove(path);

    return ok ? 0 : 1;
}
