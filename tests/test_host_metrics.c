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
    char snmp[320];
    char netstat[320];
    char net_root[320];
    char eth0[384];
    char eth1[384];
    char eth0_stats[448];
    char eth1_stats[448];
};

static int write_text(const char *path, const char *content)
{
    FILE *file = fopen(path, "wb");
    const size_t len = strlen(content);
    if (file == NULL) {
        return -1;
    }
    if (fwrite(content, 1U, len, file) != len) {
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
        snprintf(fixture->snmp, sizeof(fixture->snmp), "%s/snmp", base) < 0 ||
        snprintf(fixture->netstat, sizeof(fixture->netstat), "%s/netstat", base) < 0 ||
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

static void unlink_counter(const char *stats_dir, const char *name)
{
    char path[512];
    const int written = snprintf(path, sizeof(path), "%s/%s", stats_dir, name);
    if (written > 0 && (size_t)written < sizeof(path)) {
        unlink(path);
    }
}

static void fixture_destroy(const struct fixture *fixture)
{
    static const char *const counters[] = {
        "rx_bytes", "tx_bytes", "rx_packets", "tx_packets",
        "rx_errors", "tx_errors", "rx_dropped", "tx_dropped",
    };
    size_t index = 0U;
    for (index = 0U; index < sizeof(counters) / sizeof(counters[0]); index++) {
        unlink_counter(fixture->eth0_stats, counters[index]);
        unlink_counter(fixture->eth1_stats, counters[index]);
    }
    unlink(fixture->route);
    unlink(fixture->meminfo);
    unlink(fixture->proc_stat);
    unlink(fixture->snmp);
    unlink(fixture->netstat);
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

static int write_network_counters(const char *stats_dir, uint64_t base)
{
    char value[64];
    static const char *const names[] = {
        "rx_bytes", "tx_bytes", "rx_packets", "tx_packets",
        "rx_errors", "tx_errors", "rx_dropped", "tx_dropped",
    };
    size_t index = 0U;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); index++) {
        const int written = snprintf(value, sizeof(value), "%llu\n",
                                     (unsigned long long)(base + (uint64_t)index));
        if (written < 0 || (size_t)written >= sizeof(value) ||
            write_counter(stats_dir, names[index], value) != 0) {
            return -1;
        }
    }
    return 0;
}

static struct statd_host_paths fixture_paths(const struct fixture *fixture)
{
    struct statd_host_paths paths = {0};
    paths.storage_path = fixture->base;
    paths.route_path = fixture->route;
    paths.net_class_path = fixture->net_root;
    paths.meminfo_path = fixture->meminfo;
    paths.proc_stat_path = fixture->proc_stat;
    paths.proc_net_snmp_path = fixture->snmp;
    paths.proc_net_netstat_path = fixture->netstat;
    return paths;
}

static int write_tcp_complete(const struct fixture *fixture)
{
    CHECK(write_text(fixture->snmp,
                     "Ip: Forwarding DefaultTTL InReceives\n"
                     "Ip: 2 64 123\n"
                     "Tcp: RtoAlgorithm RtoMin RtoMax MaxConn ActiveOpens PassiveOpens AttemptFails EstabResets CurrEstab InSegs OutSegs RetransSegs InErrs OutRsts\n"
                     "Tcp: 1 200 120000 -1 100 200 3 4 7 1000 900 12 5 6\n"
                     "Udp: InDatagrams NoPorts InErrors OutDatagrams RcvbufErrors SndbufErrors\n"
                     "Udp: 700 2 3 800 4 5\n") == 0);
    CHECK(write_text(fixture->netstat,
                     "TcpExt: SyncookiesSent ListenOverflows ListenDrops TCPAbortOnMemory TCPAbortOnTimeout TCPSynRetrans TCPOrigDataSent\n"
                     "TcpExt: 1 8 9 10 11 12 800\n") == 0);
    return 0;
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
    CHECK(strcmp(paths.proc_net_snmp_path, "/proc/net/snmp") == 0);
    CHECK(strcmp(paths.proc_net_netstat_path, "/proc/net/netstat") == 0);
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
    CHECK(write_network_counters(fixture.eth1_stats, UINT64_C(1000)) == 0);
    CHECK(write_tcp_complete(&fixture) == 0);

    paths = fixture_paths(&fixture);
    CHECK(statd_host_sample(&paths, &snapshot) == STATD_HOST_OK);
    CHECK(snapshot.memory.valid);
    CHECK(snapshot.memory.total_bytes == UINT64_C(4096000) * UINT64_C(1024));
    CHECK(snapshot.memory.available_bytes == UINT64_C(1536000) * UINT64_C(1024));
    CHECK(snapshot.cpu.valid);
    CHECK(snapshot.cpu.total_ticks == UINT64_C(630));
    CHECK(snapshot.cpu.idle_ticks == UINT64_C(450));
    CHECK(snapshot.storage.valid);
    CHECK(snapshot.network.valid);
    CHECK(strcmp(snapshot.network.interface, "eth1") == 0);
    CHECK(snapshot.network.rx_bytes == UINT64_C(1000));
    CHECK(snapshot.network.tx_bytes == UINT64_C(1001));
    CHECK(snapshot.network.rx_packets == UINT64_C(1002));
    CHECK(snapshot.network.tx_packets == UINT64_C(1003));
    CHECK(snapshot.network.rx_errors == UINT64_C(1004));
    CHECK(snapshot.network.tx_errors == UINT64_C(1005));
    CHECK(snapshot.network.rx_dropped == UINT64_C(1006));
    CHECK(snapshot.network.tx_dropped == UINT64_C(1007));
    CHECK(snapshot.tcp.snmp_valid);
    CHECK(snapshot.tcp.current_established == UINT64_C(7));
    CHECK(snapshot.tcp.in_segments == UINT64_C(1000));
    CHECK(snapshot.tcp.out_segments == UINT64_C(900));
    CHECK(snapshot.tcp.retrans_segments == UINT64_C(12));
    CHECK(snapshot.tcp.in_errors == UINT64_C(5));
    CHECK(snapshot.tcp.out_resets == UINT64_C(6));
    CHECK(snapshot.tcp.attempt_fails == UINT64_C(3));
    CHECK(snapshot.tcp.established_resets == UINT64_C(4));
    CHECK(snapshot.tcp.ext_valid);
    CHECK(snapshot.tcp.syn_retrans == UINT64_C(12));
    CHECK(snapshot.tcp.listen_overflows == UINT64_C(8));
    CHECK(snapshot.tcp.listen_drops == UINT64_C(9));
    CHECK(snapshot.tcp.abort_on_memory == UINT64_C(10));
    CHECK(snapshot.tcp.abort_on_timeout == UINT64_C(11));
    CHECK(snapshot.tcp.original_data_sent == UINT64_C(800));
    CHECK(snapshot.udp.valid);
    CHECK(snapshot.udp.in_datagrams == UINT64_C(700));
    CHECK(snapshot.udp.out_datagrams == UINT64_C(800));
    CHECK(snapshot.udp.in_errors == UINT64_C(3));
    CHECK(snapshot.udp.no_ports == UINT64_C(2));
    CHECK(snapshot.udp.receive_buffer_errors == UINT64_C(4));
    CHECK(snapshot.udp.send_buffer_errors == UINT64_C(5));

    fixture_destroy(&fixture);
    return 0;
}

static int test_tcp_partial_availability(void)
{
    struct fixture fixture;
    struct statd_host_snapshot snapshot = {0};
    struct statd_host_paths paths;
    char missing[320];

    CHECK(fixture_init(&fixture) == 0);
    CHECK(write_text(fixture.snmp,
                     "Tcp: AttemptFails EstabResets CurrEstab InSegs OutSegs RetransSegs InErrs OutRsts MaxConn\n"
                     "Tcp: 1 2 3 4 5 6 7 8 -1\n") == 0);
    CHECK(snprintf(missing, sizeof(missing), "%s/missing", fixture.base) > 0);
    paths = fixture_paths(&fixture);
    paths.storage_path = missing;
    paths.route_path = missing;
    paths.net_class_path = missing;
    paths.meminfo_path = missing;
    paths.proc_stat_path = missing;
    paths.proc_net_netstat_path = missing;

    CHECK(statd_host_sample(&paths, &snapshot) == STATD_HOST_OK);
    CHECK(snapshot.tcp.snmp_valid);
    CHECK(!snapshot.tcp.ext_valid);
    CHECK(snapshot.tcp.retrans_segments == UINT64_C(6));
    CHECK(!snapshot.network.valid);

    fixture_destroy(&fixture);
    return 0;
}

static int test_missing_required_tcp_key_does_not_fabricate_data(void)
{
    struct fixture fixture;
    struct statd_host_snapshot snapshot = {0};
    struct statd_host_paths paths;
    char missing[320];

    CHECK(fixture_init(&fixture) == 0);
    CHECK(write_text(fixture.snmp,
                     "Tcp: AttemptFails EstabResets CurrEstab InSegs OutSegs InErrs OutRsts\n"
                     "Tcp: 1 2 3 4 5 7 8\n") == 0);
    CHECK(snprintf(missing, sizeof(missing), "%s/missing", fixture.base) > 0);
    paths = fixture_paths(&fixture);
    paths.storage_path = missing;
    paths.route_path = missing;
    paths.net_class_path = missing;
    paths.meminfo_path = missing;
    paths.proc_stat_path = missing;
    paths.proc_net_netstat_path = missing;

    CHECK(statd_host_sample(&paths, &snapshot) == STATD_HOST_UNAVAILABLE);
    CHECK(!snapshot.tcp.snmp_valid);
    CHECK(!snapshot.tcp.ext_valid);

    fixture_destroy(&fixture);
    return 0;
}

static int test_network_remains_independent_from_tcp(void)
{
    struct fixture fixture;
    struct statd_host_snapshot snapshot = {0};
    struct statd_host_paths paths;
    char missing[320];

    CHECK(fixture_init(&fixture) == 0);
    CHECK(write_text(fixture.route,
                     "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n"
                     "eth0\t00000000\t0100000A\t0003\t0\t0\t10\t00000000\t0\t0\t0\n") == 0);
    CHECK(write_network_counters(fixture.eth0_stats, UINT64_C(42)) == 0);
    CHECK(snprintf(missing, sizeof(missing), "%s/missing", fixture.base) > 0);

    paths = fixture_paths(&fixture);
    paths.storage_path = missing;
    paths.meminfo_path = missing;
    paths.proc_stat_path = missing;
    paths.proc_net_snmp_path = missing;
    paths.proc_net_netstat_path = missing;
    CHECK(statd_host_sample(&paths, &snapshot) == STATD_HOST_OK);
    CHECK(snapshot.network.valid);
    CHECK(snapshot.network.rx_bytes == UINT64_C(42));
    CHECK(snapshot.network.tx_dropped == UINT64_C(49));
    CHECK(!snapshot.tcp.snmp_valid);
    CHECK(!snapshot.tcp.ext_valid);

    fixture_destroy(&fixture);
    return 0;
}

static int test_missing_interface_counter_invalidates_network_only(void)
{
    struct fixture fixture;
    struct statd_host_snapshot snapshot = {0};
    struct statd_host_paths paths;
    char missing[320];

    CHECK(fixture_init(&fixture) == 0);
    CHECK(write_text(fixture.route,
                     "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n"
                     "eth0\t00000000\t0100000A\t0003\t0\t0\t10\t00000000\t0\t0\t0\n") == 0);
    CHECK(write_counter(fixture.eth0_stats, "rx_bytes", "1\n") == 0);
    CHECK(write_counter(fixture.eth0_stats, "tx_bytes", "2\n") == 0);
    CHECK(write_tcp_complete(&fixture) == 0);
    CHECK(snprintf(missing, sizeof(missing), "%s/missing", fixture.base) > 0);

    paths = fixture_paths(&fixture);
    paths.storage_path = missing;
    paths.meminfo_path = missing;
    paths.proc_stat_path = missing;
    CHECK(statd_host_sample(&paths, &snapshot) == STATD_HOST_OK);
    CHECK(!snapshot.network.valid);
    CHECK(snapshot.tcp.snmp_valid);
    CHECK(snapshot.tcp.ext_valid);

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
    paths.proc_net_snmp_path = "/definitely-not-present";
    paths.proc_net_netstat_path = "/definitely-not-present";

    CHECK(statd_host_sample(NULL, &snapshot) == STATD_HOST_INVALID);
    CHECK(statd_host_sample(&paths, NULL) == STATD_HOST_INVALID);
    CHECK(statd_host_sample(&paths, &snapshot) == STATD_HOST_UNAVAILABLE);
    return 0;
}

int main(void)
{
    CHECK(test_default_paths() == 0);
    CHECK(test_complete_snapshot() == 0);
    CHECK(test_tcp_partial_availability() == 0);
    CHECK(test_missing_required_tcp_key_does_not_fabricate_data() == 0);
    CHECK(test_network_remains_independent_from_tcp() == 0);
    CHECK(test_missing_interface_counter_invalidates_network_only() == 0);
    CHECK(test_invalid_and_fully_unavailable() == 0);
    puts("phase 6 network delivery telemetry tests passed");
    return 0;
}
