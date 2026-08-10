#include "proc_cgroup.h"

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

static int test_parser(void)
{
    char path[256];
    const char *mixed = "11:memory:/legacy\n7:cpu:/legacy-cpu\n0::/system.slice/docker-demo.scope\n";
    const char *deleted = "0::/system.slice/docker-demo.scope (deleted)\n";
    const char *duplicate = "0::/one\n0::/two\n";
    const char *legacy_only = "7:cpu:/legacy\n";
    const char *root = "0::/\n";

    CHECK(statd_parse_proc_cgroup(mixed, strlen(mixed), path, sizeof(path)) == STATD_PROC_OK);
    CHECK(strcmp(path, "system.slice/docker-demo.scope") == 0);
    CHECK(statd_parse_proc_cgroup(deleted, strlen(deleted), path, sizeof(path)) ==
          STATD_PROC_NOT_FOUND);
    CHECK(statd_parse_proc_cgroup(duplicate, strlen(duplicate), path, sizeof(path)) ==
          STATD_PROC_PARSE_ERROR);
    CHECK(statd_parse_proc_cgroup(legacy_only, strlen(legacy_only), path, sizeof(path)) ==
          STATD_PROC_NOT_FOUND);
    CHECK(statd_parse_proc_cgroup(root, strlen(root), path, sizeof(path)) == STATD_PROC_INVALID);
    CHECK(statd_parse_proc_cgroup(mixed, strlen(mixed), path, 8U) == STATD_PROC_TOO_LARGE);
    return 0;
}

static int test_resolver(void)
{
    char template_path[] = "/tmp/mypaas-statd-proc-XXXXXX";
    char *base = mkdtemp(template_path);
    char proc_root[512];
    char pid_dir[512];
    char cgroup_file[512];
    char symlink_path[512];
    char resolved[512];

    CHECK(base != NULL);
    CHECK(snprintf(proc_root, sizeof(proc_root), "%s/proc", base) > 0);
    CHECK(snprintf(pid_dir, sizeof(pid_dir), "%s/4321", proc_root) > 0);
    CHECK(snprintf(cgroup_file, sizeof(cgroup_file), "%s/cgroup", pid_dir) > 0);
    CHECK(snprintf(symlink_path, sizeof(symlink_path), "%s/9999", proc_root) > 0);
    CHECK(mkdir(proc_root, 0700) == 0);
    CHECK(mkdir(pid_dir, 0700) == 0);
    CHECK(write_text(cgroup_file, "0::/workload\n") == 0);

    CHECK(statd_proc_resolve_cgroup(proc_root, 4321U, resolved, sizeof(resolved)) ==
          STATD_PROC_OK);
    CHECK(strcmp(resolved, "workload") == 0);
    CHECK(statd_proc_resolve_cgroup(proc_root, 5555U, resolved, sizeof(resolved)) ==
          STATD_PROC_NOT_FOUND);

    CHECK(symlink(pid_dir, symlink_path) == 0);
    CHECK(statd_proc_resolve_cgroup(proc_root, 9999U, resolved, sizeof(resolved)) ==
          STATD_PROC_INVALID);
    CHECK(unlink(symlink_path) == 0);

    CHECK(unlink(cgroup_file) == 0);
    CHECK(statd_proc_resolve_cgroup(proc_root, 4321U, resolved, sizeof(resolved)) ==
          STATD_PROC_NOT_FOUND);
    CHECK(rmdir(pid_dir) == 0);
    CHECK(rmdir(proc_root) == 0);
    CHECK(rmdir(base) == 0);
    return 0;
}

int main(void)
{
    CHECK(test_parser() == 0);
    CHECK(test_resolver() == 0);
    puts("phase 4 proc cgroup resolver tests passed");
    return 0;
}
