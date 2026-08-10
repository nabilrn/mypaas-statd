#ifndef MYPAAS_STATD_PROC_CGROUP_H
#define MYPAAS_STATD_PROC_CGROUP_H

#include <stddef.h>
#include <stdint.h>

enum statd_proc_cgroup_status {
    STATD_PROC_CGROUP_OK = 0,
    STATD_PROC_CGROUP_INVALID,
    STATD_PROC_CGROUP_NOT_FOUND,
    STATD_PROC_CGROUP_TOO_LARGE,
    STATD_PROC_CGROUP_SYSTEM_ERROR
};

enum statd_proc_cgroup_status statd_proc_cgroup_parse(const char *input, size_t len, char *out,
                                                        size_t out_capacity);
enum statd_proc_cgroup_status statd_proc_cgroup_from_pid(uint64_t pid, char *out,
                                                          size_t out_capacity);

#endif
