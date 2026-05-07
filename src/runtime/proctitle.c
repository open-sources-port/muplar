/* Process-title helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include "runtime/proctitle.h"

static char *runtime_find_argv_environ_end(int argc, char **argv, char **envp)
{
    char *end = argv[0];

    for (int i = 0; i < argc; i++) {
        if (!argv[i])
            continue;

        char *next = argv[i] + strlen(argv[i]) + 1;
        if (next > end)
            end = next;
    }

    for (int i = 0; envp[i]; i++) {
        char *next = envp[i] + strlen(envp[i]) + 1;
        if (next > end)
            end = next;
    }

    return end;
}

static bool runtime_duplicate_environment(char ***out_envp)
{
    extern char **environ;
    int env_count = 0;

    while (environ[env_count])
        env_count++;

    char **new_environ =
        (char **) malloc((size_t) (env_count + 1) * sizeof(char *));
    if (!new_environ)
        return false;

    for (int i = 0; i < env_count; i++) {
        new_environ[i] = strdup(environ[i]);
        if (new_environ[i])
            continue;

        for (int j = 0; j < i; j++)
            free(new_environ[j]);
        free(new_environ);
        return false;
    }

    new_environ[env_count] = NULL;
    *out_envp = new_environ;
    return true;
}

void runtime_set_process_title(int argc, char **argv, const char *elf_path)
{
    extern char **environ;
    char **new_environ = NULL;
    size_t avail;
    const char *arch = "aarch64";
    char title[256];
    char thread_name[64];
    size_t title_len;

    if (argc <= 0 || !argv || !argv[0] || !elf_path || !environ)
        return;

    const char *slash = strrchr(elf_path, '/');
    const char *bin = slash ? slash + 1 : elf_path;

    snprintf(title, sizeof(title), "%s (%s-linux)", bin, arch);
    title_len = strlen(title);
    sysctlbyname("kern.procname", NULL, NULL, title, title_len);
    setprogname(title);

    snprintf(thread_name, sizeof(thread_name), "%s (%s-linux)", bin, arch);
    pthread_setname_np(thread_name);

    avail =
        (size_t) (runtime_find_argv_environ_end(argc, argv, environ) - argv[0]);
    if (avail == 0)
        return;

    if (!runtime_duplicate_environment(&new_environ))
        return;
    environ = new_environ;

    if (title_len < avail) {
        memcpy(argv[0], title, title_len);
        memset(argv[0] + title_len, '\0', avail - title_len);
    }
    for (int i = 1; i < argc; i++)
        argv[i] = NULL;
}
