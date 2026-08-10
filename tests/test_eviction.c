#include "sampler.h"

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

static int write_text(const char *directory, const char *name, const char *content)
{
    char path[512];
    FILE *file = NULL;
    const int written = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
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

int main(void)
{
    char template_path[] = "/tmp/mypaas-statd-eviction-XXXXXX";
    char *base = mkdtemp(template_path);
    char group[512];
    char cpu_stat[512];
    struct statd_registry registry;
    struct statd_snapshot snapshot = {0};

    CHECK(base != NULL);
    CHECK(snprintf(group, sizeof(group), "%s/workload", base) > 0);
    CHECK(mkdir(group, 0700) == 0);
    CHECK(write_text(group, "cpu.stat", "usage_usec 1000\n") == 0);
    CHECK(write_text(group, "cpu.max", "max 100000\n") == 0);
    CHECK(write_text(group, "memory.current", "4096\n") == 0);
    CHECK(write_text(group, "memory.max", "max\n") == 0);
    CHECK(write_text(group, "memory.events", "oom 0\noom_kill 0\n") == 0);
    CHECK(write_text(group, "pids.current", "1\n") == 0);
    CHECK(write_text(group, "pids.max", "max\n") == 0);

    CHECK(statd_registry_init(&registry, base) == STATD_SAMPLER_OK);
    CHECK(statd_registry_register(&registry, "runtime-1", "workload") == STATD_SAMPLER_OK);
    CHECK(statd_registry_sample(&registry, "runtime-1", &snapshot) == STATD_SAMPLER_OK);
    CHECK(registry.count == 1U);

    CHECK(snprintf(cpu_stat, sizeof(cpu_stat), "%s/cpu.stat", group) > 0);
    CHECK(unlink(cpu_stat) == 0);
    statd_registry_sample_all(&registry);

    CHECK(registry.count == 0U);
    CHECK(statd_registry_latest(&registry, "runtime-1", &snapshot) == STATD_SAMPLER_NOT_FOUND);

    statd_registry_destroy(&registry);
    unlink("/tmp/unused");

    {
        static const char *files[] = {"cpu.max",       "memory.current", "memory.max",
                                      "memory.events", "pids.current",   "pids.max"};
        size_t index = 0U;
        for (index = 0U; index < sizeof(files) / sizeof(files[0]); index++) {
            char path[512];
            const int written = snprintf(path, sizeof(path), "%s/%s", group, files[index]);
            if (written >= 0 && (size_t)written < sizeof(path)) {
                unlink(path);
            }
        }
    }
    CHECK(rmdir(group) == 0);
    CHECK(rmdir(base) == 0);

    puts("phase 4 disappeared-cgroup eviction test passed");
    return 0;
}
