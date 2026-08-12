#include "host_metrics.h"

#include <errno.h>
#include <limits.h>
#include <net/route.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

#define DEFAULT_STORAGE_PATH "/"
#define DEFAULT_ROUTE_PATH "/proc/net/route"
#define DEFAULT_NET_CLASS_PATH "/sys/class/net"
#define ROUTE_LINE_MAX 512U
#define COUNTER_TEXT_MAX 64U
#define HOST_PATH_MAX 4096U

static bool checked_product_u64(uint64_t left, uint64_t right, uint64_t *out)
{
    if (out == NULL || (right != 0U && left > UINT64_MAX / right)) {
        return false;
    }
    *out = left * right;
    return true;
}

static bool sample_storage(const char *path, struct statd_host_storage_snapshot *out)
{
    struct statvfs stats;
    uint64_t fragment_size = 0U;
    uint64_t total_bytes = 0U;
    uint64_t available_bytes = 0U;

    if (path == NULL || path[0] == '\0' || out == NULL) {
        return false;
    }
    if (statvfs(path, &stats) != 0) {
        return false;
    }

    /* statvfs(3): f_bavail is the free block count available to unprivileged callers. */
    fragment_size = (uint64_t)(stats.f_frsize != 0U ? stats.f_frsize : stats.f_bsize);
    if (fragment_size == 0U ||
        !checked_product_u64((uint64_t)stats.f_blocks, fragment_size, &total_bytes) ||
        !checked_product_u64((uint64_t)stats.f_bavail, fragment_size, &available_bytes)) {
        return false;
    }

    out->valid = true;
    out->total_bytes = total_bytes;
    out->available_bytes = available_bytes;
    return true;
}

static bool valid_interface_name(const char *name)
{
    size_t index = 0U;
    const size_t len = name == NULL ? 0U : strlen(name);
    if (len == 0U || len > STATD_HOST_INTERFACE_MAX) {
        return false;
    }
    for (index = 0U; index < len; index++) {
        const unsigned char ch = (unsigned char)name[index];
        if (ch <= 0x20U || ch == 0x7fU || ch == (unsigned char)'/') {
            return false;
        }
    }
    return true;
}

static bool parse_default_route(const char *route_path, char *interface, size_t interface_capacity)
{
    FILE *file = NULL;
    char line[ROUTE_LINE_MAX];
    bool found = false;
    unsigned long best_metric = ULONG_MAX;

    if (route_path == NULL || interface == NULL || interface_capacity == 0U) {
        return false;
    }
    file = fopen(route_path, "re");
    if (file == NULL) {
        return false;
    }

    /*
     * Linux /proc/net/route exposes the IPv4 routing table as bounded ASCII rows.
     * For host telemetry we select an UP default route (Destination 00000000),
     * preferring the lowest metric when multiple default routes exist.
     */
    while (fgets(line, sizeof(line), file) != NULL) {
        char name[STATD_HOST_INTERFACE_MAX + 1U];
        unsigned long destination = 0UL;
        unsigned long gateway = 0UL;
        unsigned long flags = 0UL;
        unsigned long ref_count = 0UL;
        unsigned long use = 0UL;
        unsigned long metric = 0UL;
        const int matched = sscanf(line, "%15s %lx %lx %lx %lu %lu %lu", name, &destination,
                                   &gateway, &flags, &ref_count, &use, &metric);
        (void)gateway;
        (void)ref_count;
        (void)use;
        if (matched != 7 || destination != 0UL || (flags & (unsigned long)RTF_UP) == 0UL ||
            !valid_interface_name(name)) {
            continue;
        }
        if (!found || metric < best_metric) {
            const int written = snprintf(interface, interface_capacity, "%s", name);
            if (written < 0 || (size_t)written >= interface_capacity) {
                fclose(file);
                return false;
            }
            found = true;
            best_metric = metric;
        }
    }

    if (ferror(file) != 0) {
        found = false;
    }
    if (fclose(file) != 0) {
        return false;
    }
    return found;
}

static bool read_u64_file(const char *path, uint64_t *out)
{
    FILE *file = NULL;
    char text[COUNTER_TEXT_MAX];
    char *end = NULL;
    unsigned long long value = 0ULL;

    if (path == NULL || out == NULL) {
        return false;
    }
    file = fopen(path, "re");
    if (file == NULL) {
        return false;
    }
    if (fgets(text, sizeof(text), file) == NULL) {
        fclose(file);
        return false;
    }
    if (fclose(file) != 0) {
        return false;
    }

    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text) {
        return false;
    }
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
        end++;
    }
    if (*end != '\0') {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static bool sample_network(const char *route_path, const char *net_class_path,
                           struct statd_host_network_snapshot *out)
{
    char interface[STATD_HOST_INTERFACE_MAX + 1U];
    char rx_path[HOST_PATH_MAX];
    char tx_path[HOST_PATH_MAX];
    uint64_t rx_bytes = 0U;
    uint64_t tx_bytes = 0U;
    int written = 0;

    if (route_path == NULL || net_class_path == NULL || out == NULL ||
        !parse_default_route(route_path, interface, sizeof(interface))) {
        return false;
    }

    written = snprintf(rx_path, sizeof(rx_path), "%s/%s/statistics/rx_bytes", net_class_path,
                       interface);
    if (written < 0 || (size_t)written >= sizeof(rx_path)) {
        return false;
    }
    written = snprintf(tx_path, sizeof(tx_path), "%s/%s/statistics/tx_bytes", net_class_path,
                       interface);
    if (written < 0 || (size_t)written >= sizeof(tx_path)) {
        return false;
    }

    /* sysfs-class-net-statistics exposes cumulative receive/transmit byte counters. */
    if (!read_u64_file(rx_path, &rx_bytes) || !read_u64_file(tx_path, &tx_bytes)) {
        return false;
    }

    written = snprintf(out->interface, sizeof(out->interface), "%s", interface);
    if (written < 0 || (size_t)written >= sizeof(out->interface)) {
        return false;
    }
    out->valid = true;
    out->rx_bytes = rx_bytes;
    out->tx_bytes = tx_bytes;
    return true;
}

void statd_host_default_paths(struct statd_host_paths *out)
{
    if (out == NULL) {
        return;
    }
    out->storage_path = DEFAULT_STORAGE_PATH;
    out->route_path = DEFAULT_ROUTE_PATH;
    out->net_class_path = DEFAULT_NET_CLASS_PATH;
}

enum statd_host_status statd_host_sample(const struct statd_host_paths *paths,
                                         struct statd_host_snapshot *out)
{
    struct statd_host_snapshot snapshot = {0};
    bool storage_ok = false;
    bool network_ok = false;

    if (paths == NULL || out == NULL || paths->storage_path == NULL || paths->route_path == NULL ||
        paths->net_class_path == NULL) {
        return STATD_HOST_INVALID;
    }

    storage_ok = sample_storage(paths->storage_path, &snapshot.storage);
    network_ok = sample_network(paths->route_path, paths->net_class_path, &snapshot.network);
    if (!storage_ok && !network_ok) {
        return STATD_HOST_UNAVAILABLE;
    }

    *out = snapshot;
    return STATD_HOST_OK;
}
