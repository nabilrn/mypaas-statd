#include "cgroup_reader.h"
#include "sampler.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
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

static int write_text_file(const char *directory, const char *name, const char *content)
{
    char path[512];
    FILE *file = NULL;
    if (snprintf(path, sizeof(path), "%s/%s", directory, name) < 0) {
        return -1;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    if (fwrite(content, 1U, strlen(content), file) != strlen(content)) {
        fclose(file);
        return -1;
    }
    return fclose(file);
}

static int make_fake_cgroup(char *root, size_t root_capacity, char *group, size_t group_capacity)
{
    char template_path[] = "/tmp/mypaas-statd-phase2-XXXXXX";
    char *created = mkdtemp(template_path);
    if (created == NULL) {
        return -1;
    }
    if (snprintf(root, root_capacity, "%s/root", created) < 0 ||
        snprintf(group, group_capacity, "%s/root/workload", created) < 0) {
        return -1;
    }
    if (mkdir(root, 0700) != 0 || mkdir(group, 0700) != 0) {
        return -1;
    }
    if (write_text_file(group, "cpu.stat", "usage_usec 1000\nuser_usec 800\nsystem_usec 200\n") != 0 ||
        write_text_file(group, "cpu.max", "50000 100000\n") != 0 ||
        write_text_file(group, "memory.current", "4096\n") != 0 ||
        write_text_file(group, "memory.max", "max\n") != 0 ||
        write_text_file(group, "memory.events", "low 0\nhigh 0\nmax 0\noom 2\noom_kill 1\n") != 0 ||
        write_text_file(group, "pids.current", "7\n") != 0 ||
        write_text_file(group, "pids.max", "64\n") != 0) {
        return -1;
    }
    return 0;
}

static void cleanup_fake_cgroup(const char *root, const char *group)
{
    static const char *files[] = {"cpu.stat",       "cpu.max",       "memory.current",
                                  "memory.max",     "memory.events", "pids.current",
                                  "pids.max",       "oversized"};
    char path[512];
    char parent[512];
    const char *slash = NULL;
    size_t index = 0U;

    for (index = 0U; index < sizeof(files) / sizeof(files[0]); index++) {
        const int written = snprintf(path, sizeof(path), "%s/%s", group, files[index]);
        if (written >= 0 && (size_t)written < sizeof(path)) {
            unlink(path);
        }
    }
    rmdir(group);
    rmdir(root);

    slash = strrchr(root, '/');
    if (slash != NULL) {
        const size_t len = (size_t)(slash - root);
        if (len < sizeof(parent)) {
            memcpy(parent, root, len);
            parent[len] = '\0';
            rmdir(parent);
        }
    }
}

static int test_safe_open_and_snapshot(void)
{
    char root[512];
    char group[512];
    char symlink_path[512];
    int fd = -1;
    struct statd_raw_snapshot raw = {0};

    CHECK(make_fake_cgroup(root, sizeof(root), group, sizeof(group)) == 0);
    CHECK(statd_cgroup_open(root, "workload", &fd) == STATD_CGROUP_OK);
    CHECK(fd >= 0);
    CHECK(statd_cgroup_read_snapshot(fd, &raw) == STATD_CGROUP_OK);
    CHECK(raw.cpu.usage_usec == 1000U);
    CHECK(raw.cpu_max.quota_usec.value == 50000U);
    CHECK(raw.memory_current_bytes == 4096U);
    CHECK(raw.memory_max.unlimited);
    CHECK(raw.memory_events.oom == 2U);
    CHECK(raw.memory_events.oom_kill == 1U);
    CHECK(raw.pids_current == 7U);
    CHECK(raw.pids_max.value == 64U);
    close(fd);

    CHECK(statd_cgroup_open(root, "/workload", &fd) == STATD_CGROUP_INVALID);
    CHECK(statd_cgroup_open(root, "../workload", &fd) == STATD_CGROUP_INVALID);
    CHECK(statd_cgroup_open(root, "workload/", &fd) == STATD_CGROUP_INVALID);
    CHECK(statd_cgroup_open(root, "workload//child", &fd) == STATD_CGROUP_INVALID);

    CHECK(snprintf(symlink_path, sizeof(symlink_path), "%s/link", root) >= 0);
    CHECK(symlink("/tmp", symlink_path) == 0);
    CHECK(statd_cgroup_open(root, "link", &fd) == STATD_CGROUP_INVALID);
    unlink(symlink_path);

    cleanup_fake_cgroup(root, group);
    return 0;
}

static int test_disappearing_and_oversized_files(void)
{
    char root[512];
    char group[512];
    char path[512];
    int fd = -1;
    struct statd_raw_snapshot raw = {0};
    FILE *file = NULL;
    size_t index = 0U;

    CHECK(make_fake_cgroup(root, sizeof(root), group, sizeof(group)) == 0);
    CHECK(statd_cgroup_open(root, "workload", &fd) == STATD_CGROUP_OK);

    CHECK(snprintf(path, sizeof(path), "%s/cpu.stat", group) >= 0);
    file = fopen(path, "wb");
    CHECK(file != NULL);
    for (index = 0U; index < STATD_CGROUP_FILE_MAX + 1U; index++) {
        CHECK(fputc('1', file) != EOF);
    }
    CHECK(fclose(file) == 0);
    CHECK(statd_cgroup_read_snapshot(fd, &raw) == STATD_CGROUP_TOO_LARGE);

    CHECK(unlink(path) == 0);
    CHECK(statd_cgroup_read_snapshot(fd, &raw) == STATD_CGROUP_NOT_FOUND);
    close(fd);
    cleanup_fake_cgroup(root, group);
    return 0;
}

static struct statd_raw_snapshot raw_sample(uint64_t cpu_usage)
{
    struct statd_raw_snapshot raw = {0};
    raw.cpu.usage_usec = cpu_usage;
    raw.cpu_max.quota_usec.value = 50000U;
    raw.cpu_max.period_usec = 100000U;
    raw.memory_current_bytes = 4096U;
    raw.memory_max.unlimited = true;
    raw.memory_events.oom = 2U;
    raw.memory_events.oom_kill = 1U;
    raw.pids_current = 7U;
    raw.pids_max.value = 64U;
    return raw;
}

static int test_sampler_state(void)
{
    struct statd_sample_state state = {0};
    struct statd_snapshot snapshot = {0};
    struct statd_raw_snapshot raw = raw_sample(1000U);
    struct timespec first = {.tv_sec = 10, .tv_nsec = 0};
    struct timespec second = {.tv_sec = 10, .tv_nsec = 100000000L};
    struct timespec same = second;
    struct timespec third = {.tv_sec = 10, .tv_nsec = 200000000L};

    statd_sample_state_init(&state);
    CHECK(statd_sample_apply(&state, &raw, first, &snapshot) == STATD_SAMPLER_OK);
    CHECK(!snapshot.cpu_percent_valid);
    CHECK(snapshot.memory_max.unlimited);

    raw.cpu.usage_usec = 51000U;
    CHECK(statd_sample_apply(&state, &raw, second, &snapshot) == STATD_SAMPLER_OK);
    CHECK(snapshot.cpu_percent_valid);
    CHECK(fabs(snapshot.cpu_percent - 50.0) < 0.000001);

    raw.cpu.usage_usec = 52000U;
    CHECK(statd_sample_apply(&state, &raw, same, &snapshot) == STATD_SAMPLER_OK);
    CHECK(!snapshot.cpu_percent_valid);

    raw.cpu.usage_usec = 52001U;
    same.tv_nsec += 1L;
    CHECK(statd_sample_apply(&state, &raw, same, &snapshot) == STATD_SAMPLER_OK);
    CHECK(snapshot.cpu_percent_valid);

    raw.cpu.usage_usec = 100U;
    CHECK(statd_sample_apply(&state, &raw, third, &snapshot) == STATD_SAMPLER_OK);
    CHECK(!snapshot.cpu_percent_valid);
    CHECK(state.previous_cpu_usage_usec == 100U);
    return 0;
}

static int test_registry_bounds_and_lifecycle(void)
{
    char root[512];
    char group[512];
    char id[32];
    struct statd_registry registry;
    struct statd_snapshot snapshot = {0};
    size_t index = 0U;

    CHECK(make_fake_cgroup(root, sizeof(root), group, sizeof(group)) == 0);
    CHECK(statd_registry_init(&registry, root) == STATD_SAMPLER_OK);
    CHECK(statd_registry_register(&registry, "runtime-0", "workload") == STATD_SAMPLER_OK);
    CHECK(statd_registry_register(&registry, "runtime-0", "workload") == STATD_SAMPLER_INVALID);
    CHECK(statd_registry_latest(&registry, "runtime-0", &snapshot) == STATD_SAMPLER_NOT_FOUND);
    CHECK(statd_registry_sample(&registry, "runtime-0", &snapshot) == STATD_SAMPLER_OK);
    CHECK(statd_registry_latest(&registry, "runtime-0", &snapshot) == STATD_SAMPLER_OK);

    for (index = 1U; index < STATD_MAX_REGISTRATIONS; index++) {
        CHECK(snprintf(id, sizeof(id), "runtime-%zu", index) >= 0);
        CHECK(statd_registry_register(&registry, id, "workload") == STATD_SAMPLER_OK);
    }
    CHECK(statd_registry_register(&registry, "overflow", "workload") == STATD_SAMPLER_LIMIT);
    CHECK(registry.count == STATD_MAX_REGISTRATIONS);

    CHECK(statd_registry_unregister(&registry, "runtime-0") == STATD_SAMPLER_OK);
    CHECK(statd_registry_unregister(&registry, "runtime-0") == STATD_SAMPLER_OK);
    CHECK(registry.count == STATD_MAX_REGISTRATIONS - 1U);
    statd_registry_destroy(&registry);
    CHECK(registry.count == 0U);

    cleanup_fake_cgroup(root, group);
    return 0;
}

int main(void)
{
    CHECK(test_safe_open_and_snapshot() == 0);
    CHECK(test_disappearing_and_oversized_files() == 0);
    CHECK(test_sampler_state() == 0);
    CHECK(test_registry_bounds_and_lifecycle() == 0);
    puts("phase 2 reader and sampler tests passed");
    return 0;
}
