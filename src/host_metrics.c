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
#define DEFAULT_MEMINFO_PATH "/proc/meminfo"
#define DEFAULT_PROC_STAT_PATH "/proc/stat"
#define ROUTE_LINE_MAX 512U
#define COUNTER_TEXT_MAX 64U
#define HOST_PATH_MAX 4096U
#define PROC_LINE_MAX 512U

static bool checked_add_u64(uint64_t left, uint64_t right, uint64_t *out)
{
    if (out == NULL || left > UINT64_MAX - right) {
        return false;
    }
    *out = left + right;
    return true;
}

static bool checked_product_u64(uint64_t left, uint64_t right, uint64_t *out)
{
    if (out == NULL || (right != 0U && left > UINT64_MAX / right)) {
        return false;
    }
    *out = left * right;
    return true;
}

static bool sample_memory(const char *path, struct statd_host_memory_snapshot *out)
{
    FILE *file = NULL;
    char line[PROC_LINE_MAX];
    uint64_t total_kib = 0U;
    uint64_t available_kib = 0U;
    bool has_total = false;
    bool has_available = false;

    if (path == NULL || path[0] == '\0' || out == NULL) {
        return false;
    }
    file = fopen(path, "re");
    if (file == NULL) {
        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL && (!has_total || !has_available)) {
        unsigned long long value = 0ULL;
        char unit[8];
        if (!has_total && sscanf(line, "MemTotal: %llu %7s", &value, unit) == 2 &&
            strcmp(unit, "kB") == 0) {
            total_kib = (uint64_t)value;
            has_total = true;
            continue;
        }
        if (!has_available && sscanf(line, "MemAvailable: %llu %7s", &value, unit) == 2 &&
            strcmp(unit, "kB") == 0) {
            available_kib = (uint64_t)value;
            has_available = true;
        }
    }

    if (ferror(file) != 0 || fclose(file) != 0 || !has_total || !has_available ||
        total_kib == 0U || available_kib > total_kib ||
        !checked_product_u64(total_kib, UINT64_C(1024), &out->total_bytes) ||
        !checked_product_u64(available_kib, UINT64_C(1024), &out->available_bytes)) {
        return false;
    }

    out->valid = true;
    return true;
}

static bool sample_cpu(const char *path, struct statd_host_cpu_snapshot *out)
{
    FILE *file = NULL;
    char line[PROC_LINE_MAX];
    unsigned long long user = 0ULL;
    unsigned long long nice = 0ULL;
    unsigned long long system = 0ULL;
    unsigned long long idle = 0ULL;
    unsigned long long iowait = 0ULL;
    unsigned long long irq = 0ULL;
    unsigned long long softirq = 0ULL;
    unsigned long long steal = 0ULL;
    uint64_t total = 0U;
    uint64_t idle_total = 0U;
    uint64_t values[8];
    size_t index = 0U;
    int matched = 0;

    if (path == NULL || path[0] == '\0' || out == NULL) {
        return false;
    }
    file = fopen(path, "re");
    if (file == NULL) {
        return false;
    }
    if (fgets(line, sizeof(line), file) == NULL || fclose(file) != 0) {
        return false;
    }

    matched = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice,
                     &system, &idle, &iowait, &irq, &softirq, &steal);
    if (matched < 4) {
        return false;
    }

    values[0] = (uint64_t)user;
    values[1] = (uint64_t)nice;
    values[2] = (uint64_t)system;
    values[3] = (uint64_t)idle;
    values[4] = matched >= 5 ? (uint64_t)iowait : 0U;
    values[5] = matched >= 6 ? (uint64_t)irq : 0U;
    values[6] = matched >= 7 ? (uint64_t)softirq : 0U;
    values[7] = matched >= 8 ? (uint64_t)steal : 0U;

    for (index = 0U; index < 8U; index++) {
        if (!checked_add_u64(total, values[index], &total)) {
            return false;
        }
    }
    if (!checked_add_u64(values[3], values[4], &idle_total) || total == 0U || idle_total > total) {
        return false;
    }

    out->valid = true;
    out->total_ticks = total;
    out->idle_ticks = idle_total;
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
    out->meminfo_path = DEFAULT_MEMINFO_PATH;
    out->proc_stat_path = DEFAULT_PROC_STAT_PATH;
}

enum statd_host_status statd_host_sample(const struct statd_host_paths *paths,
                                         struct statd_host_snapshot *out)
{
    struct statd_host_snapshot snapshot = {0};
    bool memory_ok = false;
    bool cpu_ok = false;
    bool storage_ok = false;
    bool network_ok = false;

    if (paths == NULL || out == NULL || paths->storage_path == NULL || paths->route_path == NULL ||
        paths->net_class_path == NULL || paths->meminfo_path == NULL || paths->proc_stat_path == NULL) {
        return STATD_HOST_INVALID;
    }

    memory_ok = sample_memory(paths->meminfo_path, &snapshot.memory);
    cpu_ok = sample_cpu(paths->proc_stat_path, &snapshot.cpu);
    storage_ok = sample_storage(paths->storage_path, &snapshot.storage);
    network_ok = sample_network(paths->route_path, paths->net_class_path, &snapshot.network);
    if (!memory_ok && !cpu_ok && !storage_ok && !network_ok) {
        return STATD_HOST_UNAVAILABLE;
    }

    *out = snapshot;
    return STATD_HOST_OK;
}
