#include "proc_cgroup.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
            return 1;                                                                               \
        }                                                                                           \
    } while (0)

static int test_unified_entry(void)
{
    const char input[] = "12:memory:/legacy\n0::/system.slice/docker-deadbeef.scope\n";
    char path[256];
    CHECK(statd_proc_cgroup_parse(input, sizeof(input) - 1U, path, sizeof(path)) ==
          STATD_PROC_CGROUP_OK);
    CHECK(strcmp(path, "system.slice/docker-deadbeef.scope") == 0);
    return 0;
}

static int test_nested_path(void)
{
    const char input[] = "0::/user.slice/user-1000.slice/session-2.scope\n";
    char path[256];
    CHECK(statd_proc_cgroup_parse(input, sizeof(input) - 1U, path, sizeof(path)) ==
          STATD_PROC_CGROUP_OK);
    CHECK(strcmp(path, "user.slice/user-1000.slice/session-2.scope") == 0);
    return 0;
}

static int test_invalid_entries(void)
{
    char path[256];
    const char missing[] = "2:cpu:/legacy\n";
    const char root[] = "0::/\n";
    const char deleted[] = "0::/system.slice/runtime.scope (deleted)\n";
    const char traversal[] = "0::/../other\n";
    const char duplicate[] = "0::/one\n0::/two\n";
    const char malformed[] = "0:missing-colon/path\n";

    CHECK(statd_proc_cgroup_parse(missing, sizeof(missing) - 1U, path, sizeof(path)) ==
          STATD_PROC_CGROUP_NOT_FOUND);
    CHECK(statd_proc_cgroup_parse(root, sizeof(root) - 1U, path, sizeof(path)) ==
          STATD_PROC_CGROUP_INVALID);
    CHECK(statd_proc_cgroup_parse(deleted, sizeof(deleted) - 1U, path, sizeof(path)) ==
          STATD_PROC_CGROUP_NOT_FOUND);
    CHECK(statd_proc_cgroup_parse(traversal, sizeof(traversal) - 1U, path, sizeof(path)) ==
          STATD_PROC_CGROUP_INVALID);
    CHECK(statd_proc_cgroup_parse(duplicate, sizeof(duplicate) - 1U, path, sizeof(path)) ==
          STATD_PROC_CGROUP_INVALID);
    CHECK(statd_proc_cgroup_parse(malformed, sizeof(malformed) - 1U, path, sizeof(path)) ==
          STATD_PROC_CGROUP_NOT_FOUND);
    return 0;
}

static int test_bounds(void)
{
    const char input[] = "0::/system.slice/runtime.scope\n";
    char tiny[8];
    CHECK(statd_proc_cgroup_parse(input, sizeof(input) - 1U, tiny, sizeof(tiny)) ==
          STATD_PROC_CGROUP_TOO_LARGE);
    CHECK(statd_proc_cgroup_from_pid(0U, tiny, sizeof(tiny)) == STATD_PROC_CGROUP_INVALID);
    return 0;
}

int main(void)
{
    CHECK(test_unified_entry() == 0);
    CHECK(test_nested_path() == 0);
    CHECK(test_invalid_entries() == 0);
    CHECK(test_bounds() == 0);
    puts("phase 4 proc cgroup resolution tests passed");
    return 0;
}
