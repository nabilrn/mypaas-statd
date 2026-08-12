#include "host_metrics.h"
#include "ipc.h"
#include "sampler.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MYPAAS_STATD_VERSION "0.2.0-dev"
#define DEFAULT_CGROUP_ROOT "/sys/fs/cgroup"
#define DEFAULT_SOCKET_PATH "/run/mypaas/statd.sock"
#define SAMPLE_INTERVAL_NS UINT64_C(1000000000)

static volatile sig_atomic_t g_stop_requested = 0;

static void handle_stop_signal(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction action = {0};
    action.sa_handler = handle_stop_signal;

    if (sigemptyset(&action.sa_mask) != 0) {
        return -1;
    }
    if (sigaction(SIGINT, &action, NULL) != 0) {
        return -1;
    }
    if (sigaction(SIGTERM, &action, NULL) != 0) {
        return -1;
    }
    return 0;
}

static uint64_t monotonic_ns(struct timespec value)
{
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

int main(void)
{
    const char *cgroup_root = getenv("MYPAAS_STATD_CGROUP_ROOT");
    const char *socket_path = getenv("MYPAAS_STATD_SOCKET");
    struct statd_registry registry;
    struct statd_ipc_server server;
    struct statd_host_paths host_paths;
    uint64_t next_sample_ns = 0U;

    statd_host_default_paths(&host_paths);
    if (cgroup_root == NULL || cgroup_root[0] == '\0') {
        cgroup_root = DEFAULT_CGROUP_ROOT;
    }
    if (socket_path == NULL || socket_path[0] == '\0') {
        socket_path = DEFAULT_SOCKET_PATH;
    }
    if (install_signal_handlers() != 0) {
        perror("mypaas-statd: install signal handlers");
        return EXIT_FAILURE;
    }
    if (statd_registry_init(&registry, cgroup_root) != STATD_SAMPLER_OK) {
        fprintf(stderr, "mypaas-statd: invalid cgroup root\n");
        return EXIT_FAILURE;
    }
    if (statd_ipc_server_init(&server, &registry, socket_path) != STATD_IPC_OK) {
        perror("mypaas-statd: initialize unix socket");
        statd_registry_destroy(&registry);
        return EXIT_FAILURE;
    }

    printf("mypaas-statd %s listening on %s\n", MYPAAS_STATD_VERSION, socket_path);
    while (g_stop_requested == 0) {
        struct timespec now = {0};
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            perror("mypaas-statd: clock_gettime");
            break;
        }
        if (next_sample_ns == 0U || monotonic_ns(now) >= next_sample_ns) {
            struct statd_host_snapshot host_snapshot = {0};
            statd_registry_sample_all(&registry);
            if (statd_host_sample(&host_paths, &host_snapshot) == STATD_HOST_OK) {
                statd_ipc_server_set_host_snapshot(&server, &host_snapshot);
            }
            next_sample_ns = monotonic_ns(now) + SAMPLE_INTERVAL_NS;
        }
        if (statd_ipc_server_step(&server, 250) == STATD_IPC_SYSTEM_ERROR) {
            perror("mypaas-statd: poll");
            break;
        }
    }

    statd_ipc_server_destroy(&server);
    statd_registry_destroy(&registry);
    return EXIT_SUCCESS;
}
