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
};

struct statd_host_snapshot {
    struct statd_host_storage_snapshot storage;
    struct statd_host_network_snapshot network;
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
