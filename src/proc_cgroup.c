#include "proc_cgroup.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define STATD_PROC_CGROUP_FILE_MAX 4096U

static bool invalid_component(const char *value, size_t len)
{
    return len == 0U || (len == 1U && value[0] == '.') ||
           (len == 2U && value[0] == '.' && value[1] == '.');
}

static bool valid_relative_path(const char *path, size_t len)
{
    size_t offset = 0U;
    if (len == 0U || path[0] == '/' || path[len - 1U] == '/') {
        return false;
    }
    while (offset < len) {
        size_t end = offset;
        while (end < len && path[end] != '/') {
            if (path[end] == '\0' || path[end] == '\n' || path[end] == '\r') {
                return false;
            }
            end++;
        }
        if (invalid_component(path + offset, end - offset)) {
            return false;
        }
        if (end == len) {
            break;
        }
        offset = end + 1U;
    }
    return true;
}

enum statd_proc_cgroup_status statd_proc_cgroup_parse(const char *input, size_t len, char *out,
                                                        size_t out_capacity)
{
    size_t offset = 0U;
    bool found = false;
    if (input == NULL || out == NULL || out_capacity == 0U) {
        return STATD_PROC_CGROUP_INVALID;
    }

    while (offset < len) {
        size_t line_end = offset;
        const char *path = NULL;
        size_t path_len = 0U;

        while (line_end < len && input[line_end] != '\n') {
            if (input[line_end] == '\0') {
                return STATD_PROC_CGROUP_INVALID;
            }
            line_end++;
        }
        if (line_end - offset >= 3U && input[offset] == '0' && input[offset + 1U] == ':' &&
            input[offset + 2U] == ':') {
            if (found) {
                return STATD_PROC_CGROUP_INVALID;
            }
            path = input + offset + 3U;
            path_len = line_end - (offset + 3U);
            if (path_len == 0U || path[0] != '/') {
                return STATD_PROC_CGROUP_INVALID;
            }
            if (path_len >= 10U && memcmp(path + path_len - 10U, " (deleted)", 10U) == 0) {
                return STATD_PROC_CGROUP_NOT_FOUND;
            }
            if (path_len == 1U) {
                return STATD_PROC_CGROUP_INVALID;
            }
            path++;
            path_len--;
            if (path_len + 1U > out_capacity) {
                return STATD_PROC_CGROUP_TOO_LARGE;
            }
            if (!valid_relative_path(path, path_len)) {
                return STATD_PROC_CGROUP_INVALID;
            }
            memcpy(out, path, path_len);
            out[path_len] = '\0';
            found = true;
        }
        offset = line_end < len ? line_end + 1U : line_end;
    }

    return found ? STATD_PROC_CGROUP_OK : STATD_PROC_CGROUP_NOT_FOUND;
}

enum statd_proc_cgroup_status statd_proc_cgroup_from_pid(uint64_t pid, char *out,
                                                          size_t out_capacity)
{
    char path[64];
    char buffer[STATD_PROC_CGROUP_FILE_MAX];
    size_t total = 0U;
    int fd = -1;
    int written = 0;

    if (pid == 0U || pid > (uint64_t)INT_MAX || out == NULL || out_capacity == 0U) {
        return STATD_PROC_CGROUP_INVALID;
    }
    written = snprintf(path, sizeof(path), "/proc/%llu/cgroup", (unsigned long long)pid);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return STATD_PROC_CGROUP_INVALID;
    }
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return errno == ENOENT ? STATD_PROC_CGROUP_NOT_FOUND : STATD_PROC_CGROUP_SYSTEM_ERROR;
    }

    while (total < sizeof(buffer)) {
        const ssize_t count = read(fd, buffer + total, sizeof(buffer) - total);
        if (count > 0) {
            total += (size_t)count;
            continue;
        }
        if (count == 0) {
            close(fd);
            return statd_proc_cgroup_parse(buffer, total, out, out_capacity);
        }
        if (errno != EINTR) {
            close(fd);
            return STATD_PROC_CGROUP_SYSTEM_ERROR;
        }
    }

    for (;;) {
        char extra = '\0';
        const ssize_t count = read(fd, &extra, 1U);
        if (count > 0) {
            close(fd);
            return STATD_PROC_CGROUP_TOO_LARGE;
        }
        if (count == 0) {
            close(fd);
            return statd_proc_cgroup_parse(buffer, total, out, out_capacity);
        }
        if (errno != EINTR) {
            close(fd);
            return STATD_PROC_CGROUP_SYSTEM_ERROR;
        }
    }
}
