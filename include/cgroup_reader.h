#ifndef MYPAAS_STATD_CGROUP_READER_H
#define MYPAAS_STATD_CGROUP_READER_H

#include "cgroup_parse.h"

#include <stdint.h>

#define STATD_CGROUP_PATH_MAX 4096U
#define STATD_CGROUP_FILE_MAX 4096U

enum statd_cgroup_status {
    STATD_CGROUP_OK = 0,
    STATD_CGROUP_INVALID,
    STATD_CGROUP_NOT_FOUND,
    STATD_CGROUP_TOO_LARGE,
    STATD_CGROUP_PARSE_ERROR,
    STATD_CGROUP_SYSTEM_ERROR
};

struct statd_raw_snapshot {
    struct statd_cpu_stat cpu;
    struct statd_cpu_max cpu_max;
    uint64_t memory_current_bytes;
    struct statd_limit memory_max;
    struct statd_memory_events memory_events;
    uint64_t pids_current;
    struct statd_limit pids_max;
};

enum statd_cgroup_status statd_cgroup_open(const char *root_path, const char *relative_path,
                                             int *out_dir_fd);
enum statd_cgroup_status statd_cgroup_read_snapshot(int dir_fd,
                                                     struct statd_raw_snapshot *out);

#endif
