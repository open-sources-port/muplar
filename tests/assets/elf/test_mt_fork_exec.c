#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

void *thread_func(void *arg) {
    printf("[thread] Forking from helper thread...\n");
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return NULL;
    }
    if (pid == 0) {
        printf("[child] Executing simple_app_with_print...\n");
        char *argv[] = {"/data/local/tmp/simple_app_with_print", NULL};
        char *envp[] = {NULL};
        execve("/data/local/tmp/simple_app_with_print", argv, envp);
        perror("execve failed");
        _exit(1);
    }
    int status;
    waitpid(pid, &status, 0);
    printf("[thread] Child process exited with status %d\n", status);
    return NULL;
}

int main() {
    printf("[main] Starting test_mt_fork_exec\n");
    pthread_t thread;
    if (pthread_create(&thread, NULL, thread_func, NULL) != 0) {
        perror("pthread_create failed");
        return 1;
    }
    pthread_join(thread, NULL);
    printf("[main] Finished test_mt_fork_exec\n");
    return 0;
}
