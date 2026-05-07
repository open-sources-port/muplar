/* Test fork()/waitpid() via clone syscall
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests basic fork semantics: parent forks child, child writes a message
 * and exits with a specific code, parent waits and verifies exit status.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static volatile sig_atomic_t got_usr1;

static void usr1_handler(int sig)
{
    (void) sig;
    got_usr1 = 1;
}

int main(void)
{
    printf("test-fork: forking...\n");

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* Child process */
        printf("test-fork: child (pid=%d, ppid=%d)\n", getpid(), getppid());
        _exit(42);
    }

    /* Parent process */
    printf("test-fork: parent (pid=%d), child=%d\n", getpid(), pid);

    int status;
    pid_t waited = waitpid(pid, &status, 0);
    if (waited < 0) {
        perror("waitpid");
        return 1;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 42) {
        printf("test-fork: child exited with code 42\n");
    } else {
        printf("test-fork: unexpected status 0x%x -- FAIL\n", status);
        return 1;
    }

    int ready_pipe[2];
    if (pipe(ready_pipe) < 0) {
        perror("pipe");
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork signal child");
        return 1;
    }

    if (pid == 0) {
        close(ready_pipe[0]);
        signal(SIGUSR1, usr1_handler);
        char ready = 'r';
        write(ready_pipe[1], &ready, 1);
        close(ready_pipe[1]);
        while (!got_usr1)
            usleep(10000);
        _exit(43);
    }

    close(ready_pipe[1]);
    char ready;
    if (read(ready_pipe[0], &ready, 1) != 1) {
        perror("read ready");
        close(ready_pipe[0]);
        return 1;
    }
    close(ready_pipe[0]);

    if (kill(pid, SIGUSR1) < 0) {
        perror("kill child");
        return 1;
    }

    waited = waitpid(pid, &status, 0);
    if (waited < 0) {
        perror("waitpid signal child");
        return 1;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 43) {
        printf("test-fork: child SIGUSR1 handler ran -- PASS\n");
        return 0;
    }

    printf("test-fork: signal child unexpected status 0x%x -- FAIL\n", status);
    return 1;
}
