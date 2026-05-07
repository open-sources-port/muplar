/* Validate /proc/net emulation with live sockets
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Creates TCP and UDP sockets, then reads /proc/net/tcp and /proc/net/udp
 * to verify the sockets appear with correct addresses/ports/states.
 * Also exercises /proc/net/unix, /proc/net/tcp6, and /proc/net/raw.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

static int read_proc_file(const char *path, char *buf, size_t bufsz)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("FAIL: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    ssize_t n = read(fd, buf, bufsz - 1);
    close(fd);
    if (n < 0) {
        printf("FAIL: read %s: %s\n", path, strerror(errno));
        return -1;
    }
    buf[n] = '\0';
    return 0;
}

int main(void)
{
    int pass = 0, fail = 0;
    char buf[8192];

    /* 0. Verify /proc/net exists as a directory with expected children. */
    struct stat st;
    if (stat("/proc/net", &st) == 0 && S_ISDIR(st.st_mode)) {
        DIR *dir = opendir("/proc/net");
        if (dir) {
            int found_tcp = 0, found_udp = 0, found_unix = 0;
            struct dirent *de;
            while ((de = readdir(dir))) {
                found_tcp |= !strcmp(de->d_name, "tcp");
                found_udp |= !strcmp(de->d_name, "udp");
                found_unix |= !strcmp(de->d_name, "unix");
            }
            closedir(dir);
            if (found_tcp && found_udp && found_unix) {
                printf("PASS: /proc/net enumerates synthetic socket tables\n");
                pass++;
            } else {
                printf("FAIL: /proc/net missing expected entries\n");
                fail++;
            }
        } else {
            printf("FAIL: cannot open /proc/net: %s\n", strerror(errno));
            fail++;
        }
    } else {
        printf("FAIL: /proc/net is not a directory: %s\n", strerror(errno));
        fail++;
    }

    /* 1. TCP listener on 127.0.0.1:7777 */
    int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0) {
        perror("socket(TCP)");
        return 1;
    }
    struct sockaddr_in taddr = {0};
    taddr.sin_family = AF_INET;
    taddr.sin_addr.s_addr = htonl(0x7f000001); /* 127.0.0.1 */
    taddr.sin_port = htons(7777);
    if (bind(tcp_fd, (struct sockaddr *) &taddr, sizeof(taddr)) < 0) {
        perror("bind(TCP)");
        return 1;
    }
    if (listen(tcp_fd, 1) < 0) {
        perror("listen");
        return 1;
    }

    /* 2. UDP socket on 0.0.0.0:8888 */
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        perror("socket(UDP)");
        return 1;
    }
    struct sockaddr_in uaddr = {0};
    uaddr.sin_family = AF_INET, uaddr.sin_port = htons(8888);
    if (bind(udp_fd, (struct sockaddr *) &uaddr, sizeof(uaddr)) < 0) {
        perror("bind(UDP)");
        return 1;
    }

    /* 3. Verify /proc/net/tcp */
    if (read_proc_file("/proc/net/tcp", buf, sizeof(buf)) == 0) {
        /* 7777 = 0x1E61, 127.0.0.1 = 0100007F in /proc/net format */
        if (strstr(buf, "0100007F:1E61") && strstr(buf, " 0A ")) {
            printf("PASS: /proc/net/tcp shows TCP LISTEN on 127.0.0.1:7777\n");
            pass++;
        } else {
            printf("FAIL: /proc/net/tcp missing TCP listener\n  got: %s", buf);
            fail++;
        }
    } else {
        fail++;
    }

    /* 4. Verify /proc/net/udp */
    if (read_proc_file("/proc/net/udp", buf, sizeof(buf)) == 0) {
        /* 8888 = 0x22B8 */
        if (strstr(buf, "00000000:22B8")) {
            printf("PASS: /proc/net/udp shows UDP on 0.0.0.0:8888\n");
            pass++;
        } else {
            printf("FAIL: /proc/net/udp missing UDP socket\n  got: %s", buf);
            fail++;
        }
    } else {
        fail++;
    }

    /* 5. Verify /proc/net/tcp6 opens (empty is fine, no v6 sockets) */
    if (read_proc_file("/proc/net/tcp6", buf, sizeof(buf)) == 0) {
        if (strstr(buf, "local_address")) {
            printf("PASS: /proc/net/tcp6 has valid header\n");
            pass++;
        } else {
            printf("FAIL: /proc/net/tcp6 bad header\n");
            fail++;
        }
    } else {
        fail++;
    }

    /* 6. Verify /proc/net/unix opens */
    if (read_proc_file("/proc/net/unix", buf, sizeof(buf)) == 0) {
        if (strstr(buf, "RefCount")) {
            printf("PASS: /proc/net/unix has valid header\n");
            pass++;
        } else {
            printf("FAIL: /proc/net/unix bad header\n");
            fail++;
        }
    } else {
        fail++;
    }

    /* 7. Verify /proc/net/raw opens */
    if (read_proc_file("/proc/net/raw", buf, sizeof(buf)) == 0) {
        if (strstr(buf, "local_address")) {
            printf("PASS: /proc/net/raw has valid header\n");
            pass++;
        } else {
            printf("FAIL: /proc/net/raw bad header\n");
            fail++;
        }
    } else {
        fail++;
    }

    close(tcp_fd);
    close(udp_fd);

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
