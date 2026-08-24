#ifndef MYPAAS_STATD_HOST_METRICS_H
#define MYPAAS_STATD_HOST_METRICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STATD_HOST_INTERFACE_MAX 15U

struct statd_host_paths {
    const char *storage_path;
    const char *route_path;
    const char *net_class_path;
    const char *meminfo_path;
    const char *proc_stat_path;
    const char *proc_net_snmp_path;
    const char *proc_net_netstat_path;
};

struct statd_host_memory_snapshot {
    bool valid;
    uint64_t total_bytes;
    uint64_t available_bytes;
};

struct statd_host_cpu_snapshot {
    bool valid;
    uint64_t total_ticks;
    uint64_t idle_ticks;
};

struct statd_host_storage_snapshot {
    bool valid;
    uint64_t total_bytes;
    uint64_t available_bytes;
};

struct statd_host_network_snapshot {
    bool valid;
    char interface[STATD_HOST_INTERFACE_MAX + 1U];
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t rx_dropped;
    uint64_t tx_dropped;
};

struct statd_host_tcp_snapshot {
    bool snmp_valid;
    uint64_t current_established;
    uint64_t in_segments;
    uint64_t out_segments;
    uint64_t retrans_segments;
    uint64_t in_errors;
    uint64_t out_resets;
    uint64_t attempt_fails;
    uint64_t established_resets;

    bool ext_valid;
    uint64_t syn_retrans;
    uint64_t listen_overflows;
    uint64_t listen_drops;
    uint64_t abort_on_memory;
    uint64_t abort_on_timeout;
    uint64_t original_data_sent;
};

struct statd_host_udp_snapshot {
    bool valid;
    uint64_t in_datagrams;
    uint64_t out_datagrams;
    uint64_t in_errors;
    uint64_t no_ports;
    uint64_t receive_buffer_errors;
    uint64_t send_buffer_errors;
};

struct statd_host_snapshot {
    struct statd_host_memory_snapshot memory;
    struct statd_host_cpu_snapshot cpu;
    struct statd_host_storage_snapshot storage;
    struct statd_host_network_snapshot network;
    struct statd_host_tcp_snapshot tcp;
    struct statd_host_udp_snapshot udp;
};

enum statd_host_status {
    STATD_HOST_OK = 0,
    STATD_HOST_INVALID,
    STATD_HOST_UNAVAILABLE
};

void statd_host_default_paths(struct statd_host_paths *out);
enum statd_host_status statd_host_sample(const struct statd_host_paths *paths,
                                         struct statd_host_snapshot *out);

#endif
