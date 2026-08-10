#ifndef MYPAAS_STATD_CGROUP_PARSE_H
#define MYPAAS_STATD_CGROUP_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum statd_parse_status {
    STATD_PARSE_OK = 0,
    STATD_PARSE_INVALID,
    STATD_PARSE_MISSING,
    STATD_PARSE_RANGE
};

struct statd_limit {
    bool unlimited;
    uint64_t value;
};

struct statd_cpu_stat {
    uint64_t usage_usec;
};

struct statd_cpu_max {
    struct statd_limit quota_usec;
    uint64_t period_usec;
};

struct statd_memory_events {
    uint64_t oom;
    uint64_t oom_kill;
};

enum statd_parse_status statd_parse_cpu_stat(const char *input, size_t len,
                                               struct statd_cpu_stat *out);
enum statd_parse_status statd_parse_cpu_max(const char *input, size_t len,
                                              struct statd_cpu_max *out);
enum statd_parse_status statd_parse_memory_current(const char *input, size_t len,
                                                     uint64_t *out_bytes);
enum statd_parse_status statd_parse_memory_max(const char *input, size_t len,
                                                 struct statd_limit *out);
enum statd_parse_status statd_parse_memory_events(const char *input, size_t len,
                                                    struct statd_memory_events *out);
enum statd_parse_status statd_parse_pids_current(const char *input, size_t len,
                                                   uint64_t *out_count);
enum statd_parse_status statd_parse_pids_max(const char *input, size_t len,
                                               struct statd_limit *out);

#endif
