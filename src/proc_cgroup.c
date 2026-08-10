#include "proc_cgroup.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static enum statd_proc_status status_from_errno(int error_number)
{
    if (error_number == ENOENT || error_number == ESRCH) {
        return STATD_PROC_NOT_FOUND;
    }
    if (error_number == ELOOP || error_number == ENOTDIR) {
        return STATD_PROC_INVALID;
    }
    return STATD_PROC_SYSTEM_ERROR;
}

static bool has_deleted_suffix(const char *value, size_t len)
{
    static const char suffix[] = " (deleted)";
    const size_t suffix_len = sizeof(suffix) - 1U;

    return len >= suffix_len && memcmp(value + len - suffix_len, suffix, suffix_len) == 0;
}

enum statd_proc_status statd_parse_proc_cgroup(const char *input, size_t len,
                                                 char *out_relative_path,
                                                 size_t output_capacity)
{
    size_t offset = 0U;
    bool found = false;

    if (input == NULL || out_relative_path == NULL || output_capacity == 0U) {
        return STATD_PROC_INVALID;
    }

    while (offset < len) {
        size_t line_end = offset;
        const char *path = NULL;
        size_t path_len = 0U;
        size_t relative_len = 0U;

        while (line_end < len && input[line_end] != '\n') {
            if (input[line_end] == '\0') {
                return STATD_PROC_PARSE_ERROR;
            }
            line_end++;
        }

        if (line_end - offset >= 3U && input[offset] == '0' && input[offset + 1U] == ':' &&
            input[offset + 2U] == ':') {
            if (found) {
                return STATD_PROC_PARSE_ERROR;
            }
            path = input + offset + 3U;
            path_len = line_end - (offset + 3U);
            if (path_len == 0U || path[0] != '/') {
                return STATD_PROC_PARSE_ERROR;
            }
            if (has_deleted_suffix(path, path_len)) {
                return STATD_PROC_NOT_FOUND;
            }
            if (path_len == 1U) {
                return STATD_PROC_INVALID;
            }

            relative_len = path_len - 1U;
            if (relative_len + 1U > output_capacity) {
                return STATD_PROC_TOO_LARGE;
            }
            memcpy(out_relative_path, path + 1U, relative_len);
            out_relative_path[relative_len] = '\0';
            found = true;
        }

        if (line_end == len) {
            break;
        }
        offset = line_end + 1U;
    }

    return found ? STATD_PROC_OK : STATD_PROC_NOT_FOUND;
}

static enum statd_proc_status read_cgroup_file(int proc_fd, const char *pid_text, char *buffer,
                                                size_t capacity, size_t *out_len)
{
    int pid_fd = -1;
    int cgroup_fd = -1;
    size_t total = 0U;

    pid_fd = openat(proc_fd, pid_text, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (pid_fd < 0) {
        return status_from_errno(errno);
    }
    cgroup_fd = openat(pid_fd, "cgroup", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (cgroup_fd < 0) {
        const int saved_errno = errno;
        close(pid_fd);
        return status_from_errno(saved_errno);
    }
    close(pid_fd);

    while (total < capacity) {
        const ssize_t count = read(cgroup_fd, buffer + total, capacity - total);
        if (count > 0) {
            total += (size_t)count;
            continue;
        }
        if (count == 0) {
            close(cgroup_fd);
            *out_len = total;
            return STATD_PROC_OK;
        }
        if (errno == EINTR) {
            continue;
        }
        {
            const int saved_errno = errno;
            close(cgroup_fd);
            return status_from_errno(saved_errno);
        }
    }

    for (;;) {
        char extra = '\0';
        const ssize_t count = read(cgroup_fd, &extra, 1U);
        if (count > 0) {
            close(cgroup_fd);
            return STATD_PROC_TOO_LARGE;
        }
        if (count == 0) {
            close(cgroup_fd);
            *out_len = total;
            return STATD_PROC_OK;
        }
        if (errno != EINTR) {
            const int saved_errno = errno;
            close(cgroup_fd);
            return status_from_errno(saved_errno);
        }
    }
}

enum statd_proc_status statd_proc_resolve_cgroup(const char *proc_root, uint64_t pid,
                                                   char *out_relative_path,
                                                   size_t output_capacity)
{
    char pid_text[32];
    char buffer[STATD_PROC_CGROUP_FILE_MAX];
    size_t root_len = 0U;
    size_t content_len = 0U;
    int proc_fd = -1;
    int written = 0;
    enum statd_proc_status status = STATD_PROC_OK;

    if (proc_root == NULL || out_relative_path == NULL || output_capacity == 0U || pid == 0U ||
        pid > (uint64_t)INT_MAX) {
        return STATD_PROC_INVALID;
    }
    root_len = strnlen(proc_root, STATD_PROC_ROOT_MAX + 1U);
    if (root_len == 0U || root_len > STATD_PROC_ROOT_MAX) {
        return STATD_PROC_INVALID;
    }

    written = snprintf(pid_text, sizeof(pid_text), "%llu", (unsigned long long)pid);
    if (written <= 0 || (size_t)written >= sizeof(pid_text)) {
        return STATD_PROC_INVALID;
    }

    proc_fd = open(proc_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (proc_fd < 0) {
        return status_from_errno(errno);
    }
    status = read_cgroup_file(proc_fd, pid_text, buffer, sizeof(buffer), &content_len);
    close(proc_fd);
    if (status != STATD_PROC_OK) {
        return status;
    }
    return statd_parse_proc_cgroup(buffer, content_len, out_relative_path, output_capacity);
}
