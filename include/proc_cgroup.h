#ifndef MYPAAS_STATD_PROC_CGROUP_H
#define MYPAAS_STATD_PROC_CGROUP_H

#include <stddef.h>
#include <stdint.h>

#define STATD_PROC_ROOT_MAX 4096U
#define STATD_PROC_CGROUP_FILE_MAX 8192U

enum statd_proc_status {
    STATD_PROC_OK = 0,
    STATD_PROC_INVALID,
    STATD_PROC_NOT_FOUND,
    STATD_PROC_TOO_LARGE,
    STATD_PROC_PARSE_ERROR,
    STATD_PROC_SYSTEM_ERROR
};

enum statd_proc_status statd_parse_proc_cgroup(const char *input, size_t len,
                                                 char *out_relative_path,
                                                 size_t output_capacity);
enum statd_proc_status statd_proc_resolve_cgroup(const char *proc_root, uint64_t pid,
                                                   char *out_relative_path,
                                                   size_t output_capacity);

#endif
