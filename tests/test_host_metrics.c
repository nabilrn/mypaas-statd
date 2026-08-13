#include "host_metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
            return 1;                                                                               \
        }                                                                                           \
    } while (0)

struct fixture {
    char base[256];
    char route[320];
    char meminfo[320];
    char proc_stat[320];
    char net_root[320];
    char eth0[384];
    char eth1[384];
    char eth0_stats[448];
    char eth1_stats[448];
};

static int write_text(const char *path, const char *content)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    if (fwrite(content, 1U, strlen(content), file) != strlen(content)) {
        fclose(file);
        return -1;
    }
    return fclose(file);
}

static int fixture_init(struct fixture *fixture)
{
    char template_path[] = "/tmp/mypaas-statd-host-XXXXXX";
    char *base = mkdtemp(template_path);
    if (base == NULL) {
        return -1;
    }
    memset(fixture, 0, sizeof(*fixture));
    if (snprintf(fixture->base, sizeof(fixture->base), "%s", base) < 0 ||
        snprintf(fixture->route, sizeof(fixture->route), "%s/route", base) < 0 ||
        snprintf(fixture->meminfo, sizeof(fixture->meminfo), "%s/meminfo", base) < 0 ||
        snprintf(fixture->proc_stat, sizeof(fixture->proc_stat), "%s/stat", base) < 0 ||
        snprintf(fixture->net_root, sizeof(fixture->net_root), "%s/net", base) < 0 ||
        snprintf(fixture->eth0, sizeof(fixture->eth0), "%s/eth0", fixture->net_root) < 0 ||
        snprintf(fixture->eth1, sizeof(fixture->eth1), "%s/eth1", fixture->net_root) < 0 ||
        snprintf(fixture->eth0_stats, sizeof(fixture->eth0_stats), "%s/statistics", fixture->eth0) < 0 ||
        snprintf(fixture->eth1_stats, sizeof(fixture->eth1_stats), "%s/statistics", fixture->eth1) < 0) {
        return -1;
    }
    if (mkdir(fixture->net_root, 0700) != 0 || mkdir(fixture->eth0, 0700) != 0 ||
        mkdir(fixture->eth1, 0700) != 0 || mkdir(fixture->eth0_stats, 0700) != 0 ||
        mkdir(fixture->eth1_stats, 0700) != 0) {
        return -1;
    }
    return 0;
}

static void fixture_destroy(const struct fixture *fixture)
{
    char path[512];
    if (snprintf(path, sizeof(path), "%s/rx_bytes", fixture->eth0_stats) > 0) unlink(path);
    if (snprintf(path, sizeof(path), "%s/tx_bytes", fixture->eth0_stats) > 0) unlink(path);
    if (snprintf(path, sizeof(path), "%s/rx_bytes", fixture->eth1_stats) > 0) unlink(path);
    if (snprintf(path, sizeof(path), "%s/tx_bytes", fixture->eth1_stats) > 0) unlink(path);
    unlink(fixture->route);
    unlink(fixture->meminfo);
    unlink(fixture->proc_stat);
    rmdir(fixture->eth0_stats);
    rmdir(fixture->eth1_stats);
    rmdir(fixture->eth0);
    rmdir(fixture->eth1);
    rmdir(fixture->net_root);
    rmdir(fixture->base);
}

static int write_counter(const char *stats_dir, const char *name, const char *value)
{
    char path[512];
    const int written = snprintf(path, sizeof(path), "%s/%s", stats_dir, name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return -1;
    }
    return write_text(path, value);
}

static struct statd_host_paths fixture_paths(const struct fixture *fixture)
{
    struct statd_host_paths paths = {0};
    paths.storage_path = fixture->base;
    paths.route_path = fixture->route;
    paths.net_class_path = fixture->net_root;
    paths.meminfo_path = fixture->meminfo;
    paths.proc_stat_path = fixture->proc_stat;
    return paths;
}

static int test_default_paths(void)
{
    struct statd_host_paths paths = {0};
    statd_host_default_paths(&paths);
    CHECK(strcmp(paths.storage_path, "/") == 0);
    CHECK(strcmp(paths.route_path, "/proc/net/route") == 0);
    CHECK(strcmp(paths.net_class_path, "/sys/class/net") == 0);
    CHECK(strcmp(paths.meminfo_path, "/proc/meminfo") == 0);
    CHECK(strcmp(paths.proc_stat_path, "/proc/stat") == 0);
    return 0;
}

static int test_complete_snapshot(void)
{
    struct fixture fixture;
    struct statd_host_snapshot snapshot = {0};
    struct statd_host_paths paths;

    CHECK(fixture_init(&fixture) == 0);
    CHECK(write_text(fixture.meminfo,
                     "MemTotal:        4096000 kB\n"
                     "MemFree:          256000 kB\n"
                     "MemAvailable:    1536000 kB\n") == 0);
    CHECK(write_text(fixture.proc_stat,
                     "cpu  100 20 30 400 50 10 15 5 0 0\n"
                     "cpu0 50 10 15 200 25 5 8 2 0 0\n") == 0);
    CHECK(write_text(fixture.route,
                     "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n"
                     "eth0\t00000000\t0100000A\t0003\t0\t0\t100\t00000000\t0\t0\t0\n"
                     "eth1\t00000000\t0200000A\t0003\t0\t0\t50\t00000000\t0\t0\t0\n") == 0);
    CHECK(write_counter(fixture.eth0_stats, "rx_bytes", "111\n") == 0);
    CHECK(write_counter(fixture.eth0_stats, "tx_bytes", "222\n") == 0);
    CHECK(write_counter(fixture.eth1_stats, "rx_bytes", "123456\n") == 0);
    CHECK(write_counter(fixture.eth1_stats, "tx_bytes", "789012\n") == 0);

    paths = fixture_paths(&fixture);
    CHECK(statd_host_sample(&paths, &snapshot) == STATD_HOST_OK);
    CHECK(snapshot.memory.valid);
    CHECK(snapshot.memory.total_bytes == UINT64_C(4096000) * UINT64_C(1024));
    CHECK(snapshot.memory.available_bytes == UINT64_C(1536000) * UINT64_C(1024));
    CHECK(snapshot.cpu.valid);
    CHECK(snapshot.cpu.total_ticks == UINT64_C(630));
    CHECK(snapshot.cpu.idle_ticks == UINT64_C(450));
    CHECK(snapshot.storage.valid);
    CHECK(snapshot.storage.total_bytes > 0U);
    CHECK(snapshot.storage.available_bytes <= snapshot.storage.total_bytes);
    CHECK(snapshot.network.valid);
    CHECK(strcmp(snapshot.network.interface, "eth1") == 0);
    CHECK(snapshot.network.rx_bytes == UINT64_C(123456));
    CHECK(snapshot.network.tx_bytes == UINT64_C(789012));

    fixture_destroy(&fixture);
    return 0;
}

static int test_partial_snapshot_keeps_independent_sources(void)
{
    struct fixture fixture;
    struct statd_host_snapshot snapshot = {0};
    struct statd_host_paths paths;

    CHECK(fixture_init(&fixture) == 0);
    CHECK(write_text(fixture.meminfo,
                     "MemTotal:        2048000 kB\n"
                     "MemAvailable:    1024000 kB\n") == 0);
    CHECK(write_text(fixture.route,
                     "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n"
                     "eth0\t0000000A\t00000000\t0001\t0\t0\t0\t00FFFFFF\t0\t0\t0\n") == 0);

    paths = fixture_paths(&fixture);
    CHECK(statd_host_sample(&paths, &snapshot) == STATD_HOST_OK);
    CHECK(snapshot.memory.valid);
    CHECK(!snapshot.cpu.valid);
    CHECK(snapshot.storage.valid);
    CHECK(!snapshot.network.valid);

    fixture_destroy(&fixture);
    return 0;
}

static int test_network_remains_available_when_other_paths_are_missing(void)
{
    struct fixture fixture;
    struct statd_host_snapshot snapshot = {0};
    struct statd_host_paths paths;
    char missing[320];

    CHECK(fixture_init(&fixture) == 0);
    CHECK(write_text(fixture.route,
                     "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n"
                     "eth0\t00000000\t0100000A\t0003\t0\t0\t10\t00000000\t0\t0\t0\n") == 0);
    CHECK(write_counter(fixture.eth0_stats, "rx_bytes", "42\n") == 0);
    CHECK(write_counter(fixture.eth0_stats, "tx_bytes", "84\n") == 0);
    CHECK(snprintf(missing, sizeof(missing), "%s/missing", fixture.base) > 0);

    paths = fixture_paths(&fixture);
    paths.storage_path = missing;
    paths.meminfo_path = missing;
    paths.proc_stat_path = missing;
    CHECK(statd_host_sample(&paths, &snapshot) == STATD_HOST_OK);
    CHECK(!snapshot.memory.valid);
    CHECK(!snapshot.cpu.valid);
    CHECK(!snapshot.storage.valid);
    CHECK(snapshot.network.valid);
    CHECK(snapshot.network.rx_bytes == UINT64_C(42));
    CHECK(snapshot.network.tx_bytes == UINT64_C(84));

    fixture_destroy(&fixture);
    return 0;
}

static int test_rejects_invalid_memory_and_cpu_samples(void)
{
    struct fixture fixture;
    struct statd_host_snapshot snapshot = {0};
    struct statd_host_paths paths;
    char missing[320];

    CHECK(fixture_init(&fixture) == 0);
    CHECK(write_text(fixture.meminfo,
                     "MemTotal:        1024 kB\n"
                     "MemAvailable:    2048 kB\n") == 0);
    CHECK(write_text(fixture.proc_stat, "cpu nope\n") == 0);
    CHECK(snprintf(missing, sizeof(missing), "%s/missing", fixture.base) > 0);

    paths = fixture_paths(&fixture);
    paths.storage_path = missing;
    paths.route_path = missing;
    paths.net_class_path = missing;
    CHECK(statd_host_sample(&paths, &snapshot) == STATD_HOST_UNAVAILABLE);

    fixture_destroy(&fixture);
    return 0;
}

static int test_invalid_and_fully_unavailable(void)
{
    struct statd_host_snapshot snapshot = {0};
    struct statd_host_paths paths = {0};

    paths.storage_path = "/definitely-not-present";
    paths.route_path = "/definitely-not-present";
    paths.net_class_path = "/definitely-not-present";
    paths.meminfo_path = "/definitely-not-present";
    paths.proc_stat_path = "/definitely-not-present";

    CHECK(statd_host_sample(NULL, &snapshot) == STATD_HOST_INVALID);
    CHECK(statd_host_sample(&paths, NULL) == STATD_HOST_INVALID);
    CHECK(statd_host_sample(&paths, &snapshot) == STATD_HOST_UNAVAILABLE);
    return 0;
}

int main(void)
{
    CHECK(test_default_paths() == 0);
    CHECK(test_complete_snapshot() == 0);
    CHECK(test_partial_snapshot_keeps_independent_sources() == 0);
    CHECK(test_network_remains_available_when_other_paths_are_missing() == 0);
    CHECK(test_rejects_invalid_memory_and_cpu_samples() == 0);
    CHECK(test_invalid_and_fully_unavailable() == 0);
    puts("phase 6 host telemetry tests passed");
    return 0;
}
