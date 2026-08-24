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
#define DEFAULT_PROC_NET_SNMP_PATH "/proc/net/snmp"
#define DEFAULT_PROC_NET_NETSTAT_PATH "/proc/net/netstat"
#define ROUTE_LINE_MAX 512U
#define COUNTER_TEXT_MAX 64U
#define HOST_PATH_MAX 4096U
#define PROC_LINE_MAX 512U
#define PROC_NET_LINE_MAX 8192U
#define PROC_COUNTER_KEYS_MAX 16U

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
        if (ch <= 0x20U || ch == 0x7fU || ch == (unsigned char)'/' ||
            ch == (unsigned char)'"' || ch == (unsigned char)'\\') {
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

static bool read_interface_counter(const char *net_class_path, const char *interface,
                                   const char *counter, uint64_t *out)
{
    char path[HOST_PATH_MAX];
    int written = 0;

    if (net_class_path == NULL || interface == NULL || counter == NULL || out == NULL) {
        return false;
    }
    written = snprintf(path, sizeof(path), "%s/%s/statistics/%s", net_class_path, interface,
                       counter);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return false;
    }
    return read_u64_file(path, out);
}

static bool sample_network(const char *route_path, const char *net_class_path,
                           struct statd_host_network_snapshot *out)
{
    char interface[STATD_HOST_INTERFACE_MAX + 1U];
    int written = 0;

    if (route_path == NULL || net_class_path == NULL || out == NULL ||
        !parse_default_route(route_path, interface, sizeof(interface))) {
        return false;
    }

    if (!read_interface_counter(net_class_path, interface, "rx_bytes", &out->rx_bytes) ||
        !read_interface_counter(net_class_path, interface, "tx_bytes", &out->tx_bytes) ||
        !read_interface_counter(net_class_path, interface, "rx_packets", &out->rx_packets) ||
        !read_interface_counter(net_class_path, interface, "tx_packets", &out->tx_packets) ||
        !read_interface_counter(net_class_path, interface, "rx_errors", &out->rx_errors) ||
        !read_interface_counter(net_class_path, interface, "tx_errors", &out->tx_errors) ||
        !read_interface_counter(net_class_path, interface, "rx_dropped", &out->rx_dropped) ||
        !read_interface_counter(net_class_path, interface, "tx_dropped", &out->tx_dropped)) {
        return false;
    }

    written = snprintf(out->interface, sizeof(out->interface), "%s", interface);
    if (written < 0 || (size_t)written >= sizeof(out->interface)) {
        return false;
    }
    out->valid = true;
    return true;
}

static bool line_complete(const char *line, size_t capacity, FILE *file)
{
    const size_t len = strlen(line);
    if (len > 0U && line[len - 1U] == '\n') {
        return true;
    }
    return len + 1U < capacity && feof(file) != 0;
}

static size_t requested_key_index(const char *name, const char *const *keys, size_t key_count)
{
    size_t index = 0U;
    for (index = 0U; index < key_count; index++) {
        if (strcmp(name, keys[index]) == 0) {
            return index;
        }
    }
    return key_count;
}

static bool parse_counter_value(const char *text, uint64_t *out)
{
    char *end = NULL;
    unsigned long long value = 0ULL;

    if (text == NULL || out == NULL || text[0] == '\0' || text[0] == '-') {
        return false;
    }
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static bool read_proc_counter_section(const char *path, const char *section,
                                      const char *const *keys, size_t key_count,
                                      uint64_t *values)
{
    FILE *file = NULL;
    char header[PROC_NET_LINE_MAX];
    char data[PROC_NET_LINE_MAX];
    bool found[PROC_COUNTER_KEYS_MAX] = {false};
    const size_t section_len = section == NULL ? 0U : strlen(section);
    bool success = false;

    if (path == NULL || section == NULL || keys == NULL || values == NULL || section_len == 0U ||
        key_count == 0U || key_count > PROC_COUNTER_KEYS_MAX) {
        return false;
    }
    file = fopen(path, "re");
    if (file == NULL) {
        return false;
    }

    while (fgets(header, sizeof(header), file) != NULL) {
        char *header_save = NULL;
        char *data_save = NULL;
        char *header_token = NULL;
        char *data_token = NULL;
        size_t seen = 0U;

        if (!line_complete(header, sizeof(header), file)) {
            break;
        }
        if (strncmp(header, section, section_len) != 0 || header[section_len] != ':') {
            continue;
        }
        if (fgets(data, sizeof(data), file) == NULL || !line_complete(data, sizeof(data), file) ||
            strncmp(data, section, section_len) != 0 || data[section_len] != ':') {
            break;
        }

        header_token = strtok_r(header + section_len + 1U, " \t\r\n", &header_save);
        data_token = strtok_r(data + section_len + 1U, " \t\r\n", &data_save);
        while (header_token != NULL && data_token != NULL) {
            const size_t index = requested_key_index(header_token, keys, key_count);
            if (index < key_count) {
                uint64_t parsed = 0U;
                if (found[index] || !parse_counter_value(data_token, &parsed)) {
                    goto done;
                }
                values[index] = parsed;
                found[index] = true;
                seen++;
            }
            header_token = strtok_r(NULL, " \t\r\n", &header_save);
            data_token = strtok_r(NULL, " \t\r\n", &data_save);
        }
        if (header_token != NULL || data_token != NULL || seen != key_count) {
            goto done;
        }
        success = true;
        break;
    }

done:
    if (ferror(file) != 0 || fclose(file) != 0) {
        return false;
    }
    return success;
}

static bool sample_tcp_snmp(const char *path, struct statd_host_tcp_snapshot *out)
{
    static const char *const keys[] = {
        "CurrEstab", "InSegs",     "OutSegs",      "RetransSegs",
        "InErrs",    "OutRsts",    "AttemptFails", "EstabResets",
    };
    uint64_t values[sizeof(keys) / sizeof(keys[0])] = {0};

    if (out == NULL || !read_proc_counter_section(path, "Tcp", keys,
                                                   sizeof(keys) / sizeof(keys[0]), values)) {
        return false;
    }
    out->current_established = values[0];
    out->in_segments = values[1];
    out->out_segments = values[2];
    out->retrans_segments = values[3];
    out->in_errors = values[4];
    out->out_resets = values[5];
    out->attempt_fails = values[6];
    out->established_resets = values[7];
    out->snmp_valid = true;
    return true;
}

static bool sample_tcp_ext(const char *path, struct statd_host_tcp_snapshot *out)
{
    static const char *const keys[] = {
        "TCPSynRetrans", "ListenOverflows", "ListenDrops",
        "TCPAbortOnMemory", "TCPAbortOnTimeout", "TCPOrigDataSent",
    };
    uint64_t values[sizeof(keys) / sizeof(keys[0])] = {0};

    if (out == NULL || !read_proc_counter_section(path, "TcpExt", keys,
                                                   sizeof(keys) / sizeof(keys[0]), values)) {
        return false;
    }
    out->syn_retrans = values[0];
    out->listen_overflows = values[1];
    out->listen_drops = values[2];
    out->abort_on_memory = values[3];
    out->abort_on_timeout = values[4];
    out->original_data_sent = values[5];
    out->ext_valid = true;
    return true;
}

static bool sample_tcp(const char *snmp_path, const char *netstat_path,
                       struct statd_host_tcp_snapshot *out)
{
    bool snmp_ok = false;
    bool ext_ok = false;

    if (out == NULL) {
        return false;
    }
    snmp_ok = sample_tcp_snmp(snmp_path, out);
    ext_ok = sample_tcp_ext(netstat_path, out);
    return snmp_ok || ext_ok;
}

static bool sample_udp_snmp(const char *path, struct statd_host_udp_snapshot *out)
{
    static const char *const keys[] = {
        "InDatagrams", "OutDatagrams", "InErrors",
        "NoPorts", "RcvbufErrors", "SndbufErrors",
    };
    uint64_t values[sizeof(keys) / sizeof(keys[0])] = {0};

    if (out == NULL || !read_proc_counter_section(path, "Udp", keys,
                                                   sizeof(keys) / sizeof(keys[0]), values)) {
        return false;
    }
    out->in_datagrams = values[0];
    out->out_datagrams = values[1];
    out->in_errors = values[2];
    out->no_ports = values[3];
    out->receive_buffer_errors = values[4];
    out->send_buffer_errors = values[5];
    out->valid = true;
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
    out->proc_net_snmp_path = DEFAULT_PROC_NET_SNMP_PATH;
    out->proc_net_netstat_path = DEFAULT_PROC_NET_NETSTAT_PATH;
}

enum statd_host_status statd_host_sample(const struct statd_host_paths *paths,
                                         struct statd_host_snapshot *out)
{
    struct statd_host_snapshot snapshot = {0};
    bool memory_ok = false;
    bool cpu_ok = false;
    bool storage_ok = false;
    bool network_ok = false;
    bool tcp_ok = false;
    bool udp_ok = false;

    if (paths == NULL || out == NULL || paths->storage_path == NULL || paths->route_path == NULL ||
        paths->net_class_path == NULL || paths->meminfo_path == NULL ||
        paths->proc_stat_path == NULL || paths->proc_net_snmp_path == NULL ||
        paths->proc_net_netstat_path == NULL) {
        return STATD_HOST_INVALID;
    }

    memory_ok = sample_memory(paths->meminfo_path, &snapshot.memory);
    cpu_ok = sample_cpu(paths->proc_stat_path, &snapshot.cpu);
    storage_ok = sample_storage(paths->storage_path, &snapshot.storage);
    network_ok = sample_network(paths->route_path, paths->net_class_path, &snapshot.network);
    tcp_ok = sample_tcp(paths->proc_net_snmp_path, paths->proc_net_netstat_path, &snapshot.tcp);
    udp_ok = sample_udp_snmp(paths->proc_net_snmp_path, &snapshot.udp);
    if (!memory_ok && !cpu_ok && !storage_ok && !network_ok && !tcp_ok && !udp_ok) {
        return STATD_HOST_UNAVAILABLE;
    }

    *out = snapshot;
    return STATD_HOST_OK;
}
