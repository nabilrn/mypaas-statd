#include "cgroup_reader.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

static enum statd_cgroup_status status_from_errno(int error_number)
{
    if (error_number == ENOENT) {
        return STATD_CGROUP_NOT_FOUND;
    }
    if (error_number == ELOOP || error_number == ENOTDIR) {
        return STATD_CGROUP_INVALID;
    }
    return STATD_CGROUP_SYSTEM_ERROR;
}

static bool component_invalid(const char *component, size_t len)
{
    return len == 0U || (len == 1U && component[0] == '.') ||
           (len == 2U && component[0] == '.' && component[1] == '.');
}

enum statd_cgroup_status statd_cgroup_open(const char *root_path, const char *relative_path,
                                             int *out_dir_fd)
{
    size_t path_len = 0U;
    size_t offset = 0U;
    int current_fd = -1;

    if (root_path == NULL || relative_path == NULL || out_dir_fd == NULL) {
        return STATD_CGROUP_INVALID;
    }
    *out_dir_fd = -1;
    path_len = strnlen(relative_path, STATD_CGROUP_PATH_MAX + 1U);
    if (path_len == 0U || path_len > STATD_CGROUP_PATH_MAX || relative_path[0] == '/' ||
        relative_path[path_len - 1U] == '/') {
        return STATD_CGROUP_INVALID;
    }

    current_fd = open(root_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (current_fd < 0) {
        return status_from_errno(errno);
    }

    while (offset < path_len) {
        char component[NAME_MAX + 1U];
        size_t end = offset;
        size_t component_len = 0U;
        int next_fd = -1;

        while (end < path_len && relative_path[end] != '/') {
            end++;
        }
        component_len = end - offset;
        if (component_invalid(relative_path + offset, component_len) || component_len > NAME_MAX) {
            close(current_fd);
            return STATD_CGROUP_INVALID;
        }
        memcpy(component, relative_path + offset, component_len);
        component[component_len] = '\0';

        next_fd = openat(current_fd, component,
                         O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next_fd < 0) {
            const int saved_errno = errno;
            close(current_fd);
            return status_from_errno(saved_errno);
        }
        close(current_fd);
        current_fd = next_fd;

        if (end == path_len) {
            break;
        }
        offset = end + 1U;
        if (offset == path_len || relative_path[offset] == '/') {
            close(current_fd);
            return STATD_CGROUP_INVALID;
        }
    }

    *out_dir_fd = current_fd;
    return STATD_CGROUP_OK;
}

static enum statd_cgroup_status read_file_at(int dir_fd, const char *name, char *buffer,
                                              size_t capacity, size_t *out_len)
{
    int fd = -1;
    size_t total = 0U;

    if (dir_fd < 0 || name == NULL || buffer == NULL || capacity == 0U || out_len == NULL) {
        return STATD_CGROUP_INVALID;
    }
    fd = openat(dir_fd, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return status_from_errno(errno);
    }

    while (total < capacity) {
        const ssize_t read_count = read(fd, buffer + total, capacity - total);
        if (read_count > 0) {
            total += (size_t)read_count;
            continue;
        }
        if (read_count == 0) {
            close(fd);
            *out_len = total;
            return STATD_CGROUP_OK;
        }
        if (errno == EINTR) {
            continue;
        }
        {
            const int saved_errno = errno;
            close(fd);
            return status_from_errno(saved_errno);
        }
    }

    for (;;) {
        char extra = '\0';
        const ssize_t read_count = read(fd, &extra, 1U);
        if (read_count > 0) {
            close(fd);
            return STATD_CGROUP_TOO_LARGE;
        }
        if (read_count == 0) {
            close(fd);
            *out_len = total;
            return STATD_CGROUP_OK;
        }
        if (errno != EINTR) {
            const int saved_errno = errno;
            close(fd);
            return status_from_errno(saved_errno);
        }
    }
}

static enum statd_cgroup_status parse_status(enum statd_parse_status status)
{
    return status == STATD_PARSE_OK ? STATD_CGROUP_OK : STATD_CGROUP_PARSE_ERROR;
}

#define READ_AND_PARSE(file_name, parse_call)                                                        \
    do {                                                                                             \
        status = read_file_at(dir_fd, (file_name), buffer, sizeof(buffer), &len);                    \
        if (status != STATD_CGROUP_OK) {                                                             \
            return status;                                                                           \
        }                                                                                            \
        status = parse_status((parse_call));                                                         \
        if (status != STATD_CGROUP_OK) {                                                             \
            return status;                                                                           \
        }                                                                                            \
    } while (0)

enum statd_cgroup_status statd_cgroup_read_snapshot(int dir_fd, struct statd_raw_snapshot *out)
{
    char buffer[STATD_CGROUP_FILE_MAX];
    size_t len = 0U;
    struct statd_raw_snapshot parsed = {0};
    enum statd_cgroup_status status = STATD_CGROUP_OK;

    if (dir_fd < 0 || out == NULL) {
        return STATD_CGROUP_INVALID;
    }

    READ_AND_PARSE("cpu.stat", statd_parse_cpu_stat(buffer, len, &parsed.cpu));
    READ_AND_PARSE("cpu.max", statd_parse_cpu_max(buffer, len, &parsed.cpu_max));
    READ_AND_PARSE("memory.current",
                   statd_parse_memory_current(buffer, len, &parsed.memory_current_bytes));
    READ_AND_PARSE("memory.max", statd_parse_memory_max(buffer, len, &parsed.memory_max));
    READ_AND_PARSE("memory.events",
                   statd_parse_memory_events(buffer, len, &parsed.memory_events));
    READ_AND_PARSE("pids.current", statd_parse_pids_current(buffer, len, &parsed.pids_current));
    READ_AND_PARSE("pids.max", statd_parse_pids_max(buffer, len, &parsed.pids_max));

    *out = parsed;
    return STATD_CGROUP_OK;
}
