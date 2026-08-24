#include "host_metrics.h"
#include "ipc.h"
#include "sampler.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static bool safe_json_interface(const char *value)
{
    size_t index = 0U;
    if (value == NULL || value[0] == '\0') {
        return false;
    }
    for (index = 0U; value[index] != '\0'; index++) {
        const unsigned char ch = (unsigned char)value[index];
        if (ch < 0x20U || ch > 0x7eU || ch == (unsigned char)'"' ||
            ch == (unsigned char)'\\') {
            return false;
        }
    }
    return true;
}

static int print_delivery_snapshot(void)
{
    struct statd_host_paths paths = {0};
    struct statd_host_snapshot snapshot = {0};
    int written = 0;

    statd_host_default_paths(&paths);
    if (statd_host_sample(&paths, &snapshot) != STATD_HOST_OK ||
        (!snapshot.network.valid && !snapshot.tcp.snmp_valid && !snapshot.tcp.ext_valid &&
         !snapshot.udp.valid)) {
        fprintf(stderr, "mypaas-statd: delivery metrics unavailable\n");
        return -1;
    }

    written = printf("{\"ok\":true,\"network\":");
    if (written < 0) {
        return -1;
    }
    if (snapshot.network.valid && safe_json_interface(snapshot.network.interface)) {
        written = printf(
            "{\"interface\":\"%s\",\"rx_bytes\":%llu,\"tx_bytes\":%llu,"
            "\"rx_packets\":%llu,\"tx_packets\":%llu,\"rx_errors\":%llu,"
            "\"tx_errors\":%llu,\"rx_dropped\":%llu,\"tx_dropped\":%llu}",
            snapshot.network.interface, (unsigned long long)snapshot.network.rx_bytes,
            (unsigned long long)snapshot.network.tx_bytes,
            (unsigned long long)snapshot.network.rx_packets,
            (unsigned long long)snapshot.network.tx_packets,
            (unsigned long long)snapshot.network.rx_errors,
            (unsigned long long)snapshot.network.tx_errors,
            (unsigned long long)snapshot.network.rx_dropped,
            (unsigned long long)snapshot.network.tx_dropped);
    } else {
        written = printf("null");
    }
    if (written < 0 || printf(",\"tcp\":{\"snmp\":") < 0) {
        return -1;
    }

    if (snapshot.tcp.snmp_valid) {
        written = printf(
            "{\"current_established\":%llu,\"in_segments\":%llu,"
            "\"out_segments\":%llu,\"retrans_segments\":%llu,"
            "\"in_errors\":%llu,\"out_resets\":%llu,"
            "\"attempt_fails\":%llu,\"established_resets\":%llu}",
            (unsigned long long)snapshot.tcp.current_established,
            (unsigned long long)snapshot.tcp.in_segments,
            (unsigned long long)snapshot.tcp.out_segments,
            (unsigned long long)snapshot.tcp.retrans_segments,
            (unsigned long long)snapshot.tcp.in_errors,
            (unsigned long long)snapshot.tcp.out_resets,
            (unsigned long long)snapshot.tcp.attempt_fails,
            (unsigned long long)snapshot.tcp.established_resets);
    } else {
        written = printf("null");
    }
    if (written < 0 || printf(",\"ext\":") < 0) {
        return -1;
    }

    if (snapshot.tcp.ext_valid) {
        written = printf(
            "{\"syn_retrans\":%llu,\"listen_overflows\":%llu,"
            "\"listen_drops\":%llu,\"abort_on_memory\":%llu,"
            "\"abort_on_timeout\":%llu,\"original_data_sent\":%llu}",
            (unsigned long long)snapshot.tcp.syn_retrans,
            (unsigned long long)snapshot.tcp.listen_overflows,
            (unsigned long long)snapshot.tcp.listen_drops,
            (unsigned long long)snapshot.tcp.abort_on_memory,
            (unsigned long long)snapshot.tcp.abort_on_timeout,
            (unsigned long long)snapshot.tcp.original_data_sent);
    } else {
        written = printf("null");
    }
    if (written < 0 || printf("},\"udp\":") < 0) {
        return -1;
    }

    if (snapshot.udp.valid) {
        written = printf(
            "{\"in_datagrams\":%llu,\"out_datagrams\":%llu,"
            "\"in_errors\":%llu,\"no_ports\":%llu,"
            "\"receive_buffer_errors\":%llu,\"send_buffer_errors\":%llu}",
            (unsigned long long)snapshot.udp.in_datagrams,
            (unsigned long long)snapshot.udp.out_datagrams,
            (unsigned long long)snapshot.udp.in_errors,
            (unsigned long long)snapshot.udp.no_ports,
            (unsigned long long)snapshot.udp.receive_buffer_errors,
            (unsigned long long)snapshot.udp.send_buffer_errors);
    } else {
        written = printf("null");
    }
    if (written < 0 || printf("}\n") < 0) {
        return -1;
    }
    return 0;
}

static int handle_cli(int argc, char **argv)
{
    if (argc == 1) {
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("mypaas-statd %s\n", MYPAAS_STATD_VERSION);
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "--delivery-snapshot") == 0) {
        return print_delivery_snapshot() == 0 ? 1 : -1;
    }

    fprintf(stderr, "usage: mypaas-statd [--version] [--delivery-snapshot]\n");
    return -1;
}

int main(int argc, char **argv)
{
    const int cli_status = handle_cli(argc, argv);
    const char *cgroup_root = NULL;
    const char *socket_path = NULL;
    struct statd_registry registry;
    struct statd_ipc_server server;
    struct statd_host_paths host_paths;
    uint64_t next_sample_ns = 0U;

    if (cli_status > 0) {
        return EXIT_SUCCESS;
    }
    if (cli_status < 0) {
        return EXIT_FAILURE;
    }

    cgroup_root = getenv("MYPAAS_STATD_CGROUP_ROOT");
    socket_path = getenv("MYPAAS_STATD_SOCKET");
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
            const enum statd_host_status host_status = statd_host_sample(&host_paths, &host_snapshot);
            statd_registry_sample_all(&registry);
            if (host_status == STATD_HOST_OK) {
                statd_ipc_server_set_host_snapshot(&server, &host_snapshot);
            } else {
                /* Do not let a completely failed host collection look like fresh idle telemetry. */
                server.has_host_snapshot = false;
                server.host_snapshot = (struct statd_host_snapshot){0};
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
