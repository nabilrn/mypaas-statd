#include "cgroup_parse.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
            return 1;                                                                               \
        }                                                                                           \
    } while (0)

static char *read_fixture(const char *path, size_t *out_len)
{
    FILE *file = NULL;
    long size = 0L;
    size_t read_len = 0U;
    char *buffer = NULL;

    if (path == NULL || out_len == NULL) {
        return NULL;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    buffer = malloc((size_t)size + 1U);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }
    read_len = fread(buffer, 1U, (size_t)size, file);
    if (read_len != (size_t)size || fclose(file) != 0) {
        free(buffer);
        return NULL;
    }
    buffer[read_len] = '\0';
    *out_len = read_len;
    return buffer;
}

static int test_cpu_stat_fixture(void)
{
    size_t len = 0U;
    char *input = read_fixture("fixtures/cgroup/cpu.stat", &len);
    struct statd_cpu_stat parsed = {0};
    CHECK(input != NULL);
    CHECK(statd_parse_cpu_stat(input, len, &parsed) == STATD_PARSE_OK);
    CHECK(parsed.usage_usec == UINT64_C(1234567));
    free(input);
    return 0;
}

static int test_cpu_stat_edges(void)
{
    struct statd_cpu_stat parsed = {0};
    const char missing[] = "user_usec 1\nsystem_usec 2\n";
    const char malformed[] = "usage_usec nope\n";
    const char overflow[] = "usage_usec 18446744073709551616\n";
    const char duplicate[] = "usage_usec 1\nusage_usec 2\n";
    const char whitespace[] = "\n  usage_usec\t42  \r\nunknown 99\n";

    CHECK(statd_parse_cpu_stat(missing, sizeof(missing) - 1U, &parsed) == STATD_PARSE_MISSING);
    CHECK(statd_parse_cpu_stat(malformed, sizeof(malformed) - 1U, &parsed) == STATD_PARSE_INVALID);
    CHECK(statd_parse_cpu_stat(overflow, sizeof(overflow) - 1U, &parsed) == STATD_PARSE_RANGE);
    CHECK(statd_parse_cpu_stat(duplicate, sizeof(duplicate) - 1U, &parsed) == STATD_PARSE_INVALID);
    CHECK(statd_parse_cpu_stat(whitespace, sizeof(whitespace) - 1U, &parsed) == STATD_PARSE_OK);
    CHECK(parsed.usage_usec == UINT64_C(42));
    return 0;
}

static int test_cpu_max(void)
{
    struct statd_cpu_max parsed = {0};
    const char limited[] = "50000 100000\n";
    const char unlimited[] = " max\t100000 \n";
    const char missing_period[] = "50000\n";
    const char extra[] = "50000 100000 extra\n";
    const char overflow[] = "18446744073709551616 100000\n";

    CHECK(statd_parse_cpu_max(limited, sizeof(limited) - 1U, &parsed) == STATD_PARSE_OK);
    CHECK(!parsed.quota_usec.unlimited);
    CHECK(parsed.quota_usec.value == UINT64_C(50000));
    CHECK(parsed.period_usec == UINT64_C(100000));
    CHECK(statd_parse_cpu_max(unlimited, sizeof(unlimited) - 1U, &parsed) == STATD_PARSE_OK);
    CHECK(parsed.quota_usec.unlimited);
    CHECK(parsed.period_usec == UINT64_C(100000));
    CHECK(statd_parse_cpu_max(missing_period, sizeof(missing_period) - 1U, &parsed) == STATD_PARSE_INVALID);
    CHECK(statd_parse_cpu_max(extra, sizeof(extra) - 1U, &parsed) == STATD_PARSE_INVALID);
    CHECK(statd_parse_cpu_max(overflow, sizeof(overflow) - 1U, &parsed) == STATD_PARSE_RANGE);
    return 0;
}

static int test_memory_values(void)
{
    uint64_t current = 0U;
    struct statd_limit limit = {0};
    const char current_input[] = "  4096\n";
    const char limit_input[] = "1048576\n";
    const char unlimited[] = " max \n";
    const char malformed[] = "12MB\n";

    CHECK(statd_parse_memory_current(current_input, sizeof(current_input) - 1U, &current) == STATD_PARSE_OK);
    CHECK(current == UINT64_C(4096));
    CHECK(statd_parse_memory_max(limit_input, sizeof(limit_input) - 1U, &limit) == STATD_PARSE_OK);
    CHECK(!limit.unlimited && limit.value == UINT64_C(1048576));
    CHECK(statd_parse_memory_max(unlimited, sizeof(unlimited) - 1U, &limit) == STATD_PARSE_OK);
    CHECK(limit.unlimited);
    CHECK(statd_parse_memory_current(malformed, sizeof(malformed) - 1U, &current) == STATD_PARSE_INVALID);
    return 0;
}

static int test_memory_events_fixture(void)
{
    size_t len = 0U;
    char *input = read_fixture("fixtures/cgroup/memory.events", &len);
    struct statd_memory_events parsed = {0};
    CHECK(input != NULL);
    CHECK(statd_parse_memory_events(input, len, &parsed) == STATD_PARSE_OK);
    CHECK(parsed.oom == UINT64_C(3));
    CHECK(parsed.oom_kill == UINT64_C(2));
    free(input);
    return 0;
}

static int test_memory_events_edges(void)
{
    struct statd_memory_events parsed = {0};
    const char missing[] = "low 0\noom 1\n";
    const char unknown[] = "future_counter 999\noom_kill 7\noom 8\n";
    const char malformed[] = "oom x\noom_kill 1\n";

    CHECK(statd_parse_memory_events(missing, sizeof(missing) - 1U, &parsed) == STATD_PARSE_MISSING);
    CHECK(statd_parse_memory_events(unknown, sizeof(unknown) - 1U, &parsed) == STATD_PARSE_OK);
    CHECK(parsed.oom == UINT64_C(8) && parsed.oom_kill == UINT64_C(7));
    CHECK(statd_parse_memory_events(malformed, sizeof(malformed) - 1U, &parsed) == STATD_PARSE_INVALID);
    return 0;
}

static int test_pids_values(void)
{
    uint64_t current = 0U;
    struct statd_limit limit = {0};
    const char current_input[] = "17\n";
    const char limited[] = "64\n";
    const char unlimited[] = "max\n";
    const char overflow[] = "18446744073709551616\n";

    CHECK(statd_parse_pids_current(current_input, sizeof(current_input) - 1U, &current) == STATD_PARSE_OK);
    CHECK(current == UINT64_C(17));
    CHECK(statd_parse_pids_max(limited, sizeof(limited) - 1U, &limit) == STATD_PARSE_OK);
    CHECK(!limit.unlimited && limit.value == UINT64_C(64));
    CHECK(statd_parse_pids_max(unlimited, sizeof(unlimited) - 1U, &limit) == STATD_PARSE_OK);
    CHECK(limit.unlimited);
    CHECK(statd_parse_pids_current(overflow, sizeof(overflow) - 1U, &current) == STATD_PARSE_RANGE);
    return 0;
}

int main(void)
{
    CHECK(test_cpu_stat_fixture() == 0);
    CHECK(test_cpu_stat_edges() == 0);
    CHECK(test_cpu_max() == 0);
    CHECK(test_memory_values() == 0);
    CHECK(test_memory_events_fixture() == 0);
    CHECK(test_memory_events_edges() == 0);
    CHECK(test_pids_values() == 0);
    puts("phase 1 cgroup parser tests passed");
    return 0;
}
