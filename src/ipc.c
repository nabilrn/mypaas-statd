#include "ipc.h"
#include "proc_cgroup.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

enum request_operation {
    REQUEST_NONE = 0,
    REQUEST_HELLO,
    REQUEST_REGISTER,
    REQUEST_UNREGISTER,
    REQUEST_SNAPSHOT,
    REQUEST_STATUS
};

struct request {
    enum request_operation operation;
    bool has_protocol;
    uint64_t protocol;
    bool has_id;
    char id[STATD_RUNTIME_ID_MAX + 1U];
    bool has_cgroup;
    char cgroup[STATD_CGROUP_PATH_MAX + 1U];
    bool has_pid;
    uint64_t pid;
};

static bool ascii_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static void skip_space(const char *input, size_t len, size_t *offset)
{
    while (*offset < len && ascii_space(input[*offset])) {
        (*offset)++;
    }
}

static bool parse_ascii_string(const char *input, size_t len, size_t *offset, char *output,
                               size_t output_capacity)
{
    size_t written = 0U;
    if (*offset >= len || input[*offset] != '"' || output_capacity == 0U) {
        return false;
    }
    (*offset)++;
    while (*offset < len && input[*offset] != '"') {
        const unsigned char ch = (unsigned char)input[*offset];
        if (ch < 0x20U || ch > 0x7eU || ch == (unsigned char)'\\' ||
            written + 1U >= output_capacity) {
            return false;
        }
        output[written++] = (char)ch;
        (*offset)++;
    }
    if (*offset >= len || input[*offset] != '"') {
        return false;
    }
    (*offset)++;
    output[written] = '\0';
    return true;
}

static bool parse_u64_json(const char *input, size_t len, size_t *offset, uint64_t *out)
{
    uint64_t result = 0U;
    size_t digits = 0U;
    if (out == NULL) {
        return false;
    }
    while (*offset < len && input[*offset] >= '0' && input[*offset] <= '9') {
        const uint64_t digit = (uint64_t)((unsigned char)input[*offset] - (unsigned char)'0');
        if (result > (UINT64_MAX - digit) / UINT64_C(10)) {
            return false;
        }
        result = result * UINT64_C(10) + digit;
        (*offset)++;
        digits++;
    }
    if (digits == 0U) {
        return false;
    }
    *out = result;
    return true;
}

static enum request_operation operation_from_string(const char *value)
{
    if (strcmp(value, "hello") == 0) {
        return REQUEST_HELLO;
    }
    if (strcmp(value, "register") == 0) {
        return REQUEST_REGISTER;
    }
    if (strcmp(value, "unregister") == 0) {
        return REQUEST_UNREGISTER;
    }
    if (strcmp(value, "snapshot") == 0) {
        return REQUEST_SNAPSHOT;
    }
    if (strcmp(value, "status") == 0) {
        return REQUEST_STATUS;
    }
    return REQUEST_NONE;
}

static bool parse_request(const char *input, size_t len, struct request *out)
{
    size_t offset = 0U;
    bool has_op = false;
    bool has_protocol = false;
    bool has_id = false;
    bool has_cgroup = false;
    bool has_pid = false;
    char op_value[16];
    struct request parsed = {0};

    if (input == NULL || out == NULL) {
        return false;
    }
    skip_space(input, len, &offset);
    if (offset >= len || input[offset++] != '{') {
        return false;
    }
    skip_space(input, len, &offset);
    if (offset < len && input[offset] == '}') {
        return false;
    }

    for (;;) {
        char key[16];
        skip_space(input, len, &offset);
        if (!parse_ascii_string(input, len, &offset, key, sizeof(key))) {
            return false;
        }
        skip_space(input, len, &offset);
        if (offset >= len || input[offset++] != ':') {
            return false;
        }
        skip_space(input, len, &offset);

        if (strcmp(key, "op") == 0) {
            if (has_op || !parse_ascii_string(input, len, &offset, op_value, sizeof(op_value))) {
                return false;
            }
            parsed.operation = operation_from_string(op_value);
            if (parsed.operation == REQUEST_NONE) {
                return false;
            }
            has_op = true;
        } else if (strcmp(key, "protocol") == 0) {
            if (has_protocol || !parse_u64_json(input, len, &offset, &parsed.protocol)) {
                return false;
            }
            parsed.has_protocol = true;
            has_protocol = true;
        } else if (strcmp(key, "id") == 0) {
            if (has_id || !parse_ascii_string(input, len, &offset, parsed.id, sizeof(parsed.id)) ||
                parsed.id[0] == '\0') {
                return false;
            }
            parsed.has_id = true;
            has_id = true;
        } else if (strcmp(key, "cgroup") == 0) {
            if (has_cgroup ||
                !parse_ascii_string(input, len, &offset, parsed.cgroup, sizeof(parsed.cgroup)) ||
                parsed.cgroup[0] == '\0') {
                return false;
            }
            parsed.has_cgroup = true;
            has_cgroup = true;
        } else if (strcmp(key, "pid") == 0) {
            if (has_pid || !parse_u64_json(input, len, &offset, &parsed.pid) || parsed.pid == 0U) {
                return false;
            }
            parsed.has_pid = true;
            has_pid = true;
        } else {
            return false;
        }

        skip_space(input, len, &offset);
        if (offset >= len) {
            return false;
        }
        if (input[offset] == '}') {
            offset++;
            break;
        }
        if (input[offset++] != ',') {
            return false;
        }
    }

    skip_space(input, len, &offset);
    if (offset != len || !has_op) {
        return false;
    }
    *out = parsed;
    return true;
}

static void client_reset(struct statd_ipc_client *client)
{
    memset(client, 0, sizeof(*client));
    client->fd = -1;
}

static void client_close(struct statd_ipc_client *client)
{
    if (client->fd >= 0) {
        close(client->fd);
    }
    client_reset(client);
}

static void queue_text(struct statd_ipc_client *client, const char *text, bool close_after)
{
    const size_t len = strlen(text);
    if (len >= sizeof(client->output)) {
        client_close(client);
        return;
    }
    memcpy(client->output, text, len);
    client->output_len = len;
    client->output_offset = 0U;
    client->close_after_write = close_after;
}

static void queue_error(struct statd_ipc_client *client, const char *code, bool close_after)
{
    const int written = snprintf(client->output, sizeof(client->output),
                                 "{\"ok\":false,\"error\":{\"code\":\"%s\"}}\n", code);
    if (written < 0 || (size_t)written >= sizeof(client->output)) {
        client_close(client);
        return;
    }
    client->output_len = (size_t)written;
    client->output_offset = 0U;
    client->close_after_write = close_after;
}

static void queue_snapshot(struct statd_ipc_client *client, const struct statd_snapshot *snapshot,
                           bool stale)
{
    char cpu_percent[64];
    char cpu_quota[32];
    char memory_max[32];
    char pids_max[32];
    int written = 0;

    if (snapshot->cpu_percent_valid) {
        written = snprintf(cpu_percent, sizeof(cpu_percent), "%.6f", snapshot->cpu_percent);
    } else {
        written = snprintf(cpu_percent, sizeof(cpu_percent), "null");
    }
    if (written < 0 || (size_t)written >= sizeof(cpu_percent)) {
        client_close(client);
        return;
    }
    if (snapshot->cpu_max.quota_usec.unlimited) {
        written = snprintf(cpu_quota, sizeof(cpu_quota), "null");
    } else {
        written = snprintf(cpu_quota, sizeof(cpu_quota), "%llu",
                           (unsigned long long)snapshot->cpu_max.quota_usec.value);
    }
    if (written < 0 || (size_t)written >= sizeof(cpu_quota)) {
        client_close(client);
        return;
    }
    if (snapshot->memory_max.unlimited) {
        written = snprintf(memory_max, sizeof(memory_max), "null");
    } else {
        written = snprintf(memory_max, sizeof(memory_max), "%llu",
                           (unsigned long long)snapshot->memory_max.value);
    }
    if (written < 0 || (size_t)written >= sizeof(memory_max)) {
        client_close(client);
        return;
    }
    if (snapshot->pids_max.unlimited) {
        written = snprintf(pids_max, sizeof(pids_max), "null");
    } else {
        written = snprintf(pids_max, sizeof(pids_max), "%llu",
                           (unsigned long long)snapshot->pids_max.value);
    }
    if (written < 0 || (size_t)written >= sizeof(pids_max)) {
        client_close(client);
        return;
    }

    written = snprintf(
        client->output, sizeof(client->output),
        "{\"ok\":true,\"protocol\":1,\"valid\":true,\"stale\":%s,"
        "\"cpu\":{\"usage_usec\":%llu,\"percent\":%s,\"quota_usec\":%s,"
        "\"period_usec\":%llu},\"memory\":{\"current_bytes\":%llu,\"max_bytes\":%s,"
        "\"oom\":%llu,\"oom_kill\":%llu},\"pids\":{\"current\":%llu,\"max\":%s}}\n",
        stale ? "true" : "false", (unsigned long long)snapshot->cpu_usage_usec, cpu_percent,
        cpu_quota, (unsigned long long)snapshot->cpu_max.period_usec,
        (unsigned long long)snapshot->memory_current_bytes, memory_max,
        (unsigned long long)snapshot->memory_events.oom,
        (unsigned long long)snapshot->memory_events.oom_kill,
        (unsigned long long)snapshot->pids_current, pids_max);
    if (written < 0 || (size_t)written >= sizeof(client->output)) {
        client_close(client);
        return;
    }
    client->output_len = (size_t)written;
    client->output_offset = 0U;
}

static void handle_request(struct statd_ipc_server *server, struct statd_ipc_client *client,
                           const char *message, size_t message_len)
{
    struct request request = {0};

    if (!parse_request(message, message_len, &request)) {
        queue_error(client, "INVALID_REQUEST", false);
        return;
    }
    if (request.operation == REQUEST_HELLO) {
        if (!request.has_protocol || request.protocol != STATD_PROTOCOL_VERSION) {
            queue_error(client, "UNSUPPORTED_PROTOCOL", false);
            return;
        }
        client->negotiated = true;
        queue_text(client,
                   "{\"ok\":true,\"protocol\":1,\"agent\":\"mypaas-statd\","
                   "\"version\":\"0.1.0-dev\"}\n",
                   false);
        return;
    }
    if (!client->negotiated) {
        queue_error(client, "HELLO_REQUIRED", false);
        return;
    }

    if (request.operation == REQUEST_STATUS) {
        const int written = snprintf(client->output, sizeof(client->output),
                                     "{\"ok\":true,\"protocol\":1,\"registrations\":%zu}\n",
                                     server->registry->count);
        if (written < 0 || (size_t)written >= sizeof(client->output)) {
            client_close(client);
            return;
        }
        client->output_len = (size_t)written;
        client->output_offset = 0U;
        return;
    }

    if (!request.has_id) {
        queue_error(client, "INVALID_REQUEST", false);
        return;
    }
    if (request.operation == REQUEST_REGISTER) {
        char resolved_cgroup[STATD_CGROUP_PATH_MAX + 1U];
        const char *cgroup = request.cgroup;
        struct statd_snapshot ignored = {0};
        enum statd_sampler_status status = STATD_SAMPLER_OK;

        if (request.has_pid == request.has_cgroup) {
            queue_error(client, "INVALID_REQUEST", false);
            return;
        }
        if (request.has_pid) {
            const enum statd_proc_cgroup_status proc_status =
                statd_proc_cgroup_from_pid(request.pid, resolved_cgroup, sizeof(resolved_cgroup));
            if (proc_status == STATD_PROC_CGROUP_NOT_FOUND) {
                queue_error(client, "CGROUP_NOT_FOUND", false);
                return;
            }
            if (proc_status != STATD_PROC_CGROUP_OK) {
                queue_error(client, "CGROUP_RESOLVE_FAILED", false);
                return;
            }
            cgroup = resolved_cgroup;
        }

        status = statd_registry_register(server->registry, request.id, cgroup);
        if (status == STATD_SAMPLER_LIMIT) {
            queue_error(client, "REGISTRATION_LIMIT", false);
            return;
        }
        if (status == STATD_SAMPLER_NOT_FOUND) {
            queue_error(client, "CGROUP_NOT_FOUND", false);
            return;
        }
        if (status != STATD_SAMPLER_OK) {
            queue_error(client, "REGISTER_FAILED", false);
            return;
        }
        (void)statd_registry_sample(server->registry, request.id, &ignored);
        queue_text(client, "{\"ok\":true,\"protocol\":1}\n", false);
        return;
    }
    if (request.operation == REQUEST_UNREGISTER) {
        if (statd_registry_unregister(server->registry, request.id) != STATD_SAMPLER_OK) {
            queue_error(client, "UNREGISTER_FAILED", false);
            return;
        }
        queue_text(client, "{\"ok\":true,\"protocol\":1}\n", false);
        return;
    }
    if (request.operation == REQUEST_SNAPSHOT) {
        bool has_snapshot = false;
        enum statd_sampler_status last_status = STATD_SAMPLER_OK;
        struct statd_snapshot snapshot = {0};
        if (statd_registry_runtime_state(server->registry, request.id, &has_snapshot,
                                         &last_status) != STATD_SAMPLER_OK) {
            queue_error(client, "NOT_FOUND", false);
            return;
        }
        if (!has_snapshot || statd_registry_latest(server->registry, request.id, &snapshot) !=
                                 STATD_SAMPLER_OK) {
            queue_error(client, "METRICS_UNAVAILABLE", false);
            return;
        }
        queue_snapshot(client, &snapshot, last_status != STATD_SAMPLER_OK);
        return;
    }

    queue_error(client, "INVALID_REQUEST", false);
}

static bool find_line(const char *input, size_t len, size_t *line_len)
{
    size_t index = 0U;
    for (index = 0U; index < len; index++) {
        if (input[index] == '\0') {
            return false;
        }
        if (input[index] == '\n') {
            *line_len = index;
            return true;
        }
    }
    return false;
}

static void process_buffered_message(struct statd_ipc_server *server,
                                     struct statd_ipc_client *client)
{
    size_t line_len = 0U;
    size_t consumed = 0U;
    if (client->fd < 0 || client->output_len != 0U) {
        return;
    }
    if (!find_line(client->input, client->input_len, &line_len)) {
        return;
    }
    handle_request(server, client, client->input, line_len);
    if (client->fd < 0) {
        return;
    }
    consumed = line_len + 1U;
    memmove(client->input, client->input + consumed, client->input_len - consumed);
    client->input_len -= consumed;
}

static void handle_read(struct statd_ipc_server *server, struct statd_ipc_client *client)
{
    ssize_t count = 0;
    if (client->input_len == sizeof(client->input)) {
        queue_error(client, "MESSAGE_TOO_LARGE", true);
        return;
    }
    count = recv(client->fd, client->input + client->input_len,
                 sizeof(client->input) - client->input_len, 0);
    if (count > 0) {
        const size_t old_len = client->input_len;
        const size_t new_bytes = (size_t)count;
        size_t index = 0U;
        client->input_len += new_bytes;
        for (index = old_len; index < client->input_len; index++) {
            if (client->input[index] == '\0') {
                queue_error(client, "INVALID_REQUEST", true);
                return;
            }
        }
        process_buffered_message(server, client);
        if (client->output_len == 0U && client->input_len == sizeof(client->input)) {
            queue_error(client, "MESSAGE_TOO_LARGE", true);
        }
        return;
    }
    if (count == 0) {
        client_close(client);
        return;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        client_close(client);
    }
}

static void handle_write(struct statd_ipc_server *server, struct statd_ipc_client *client)
{
    const size_t remaining = client->output_len - client->output_offset;
    const ssize_t count = send(client->fd, client->output + client->output_offset, remaining,
                               MSG_NOSIGNAL);
    if (count > 0) {
        client->output_offset += (size_t)count;
        if (client->output_offset == client->output_len) {
            const bool should_close = client->close_after_write;
            client->output_len = 0U;
            client->output_offset = 0U;
            client->close_after_write = false;
            if (should_close) {
                client_close(client);
                return;
            }
            process_buffered_message(server, client);
        }
        return;
    }
    if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        client_close(client);
    }
}

static void accept_clients(struct statd_ipc_server *server)
{
    for (;;) {
        int client_fd = accept4(server->listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        size_t slot = 0U;
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return;
            }
            return;
        }
        for (slot = 0U; slot < STATD_IPC_MAX_CLIENTS; slot++) {
            if (server->clients[slot].fd < 0) {
                client_reset(&server->clients[slot]);
                server->clients[slot].fd = client_fd;
                client_fd = -1;
                break;
            }
        }
        if (client_fd >= 0) {
            close(client_fd);
        }
    }
}

enum statd_ipc_status statd_ipc_server_init(struct statd_ipc_server *server,
                                              struct statd_registry *registry,
                                              const char *socket_path)
{
    struct sockaddr_un address;
    struct stat st;
    size_t path_len = 0U;
    size_t index = 0U;

    if (server == NULL || registry == NULL || socket_path == NULL) {
        return STATD_IPC_INVALID;
    }
    path_len = strnlen(socket_path, STATD_IPC_SOCKET_PATH_MAX + 1U);
    if (path_len == 0U || path_len > STATD_IPC_SOCKET_PATH_MAX) {
        return STATD_IPC_INVALID;
    }

    memset(server, 0, sizeof(*server));
    server->listen_fd = -1;
    server->registry = registry;
    for (index = 0U; index < STATD_IPC_MAX_CLIENTS; index++) {
        client_reset(&server->clients[index]);
    }

    if (lstat(socket_path, &st) == 0) {
        if (!S_ISSOCK(st.st_mode) || unlink(socket_path) != 0) {
            return STATD_IPC_INVALID;
        }
    } else if (errno != ENOENT) {
        return STATD_IPC_SYSTEM_ERROR;
    }

    server->listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (server->listen_fd < 0) {
        return STATD_IPC_SYSTEM_ERROR;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, socket_path, path_len + 1U);
    if (bind(server->listen_fd, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        chmod(socket_path, S_IRUSR | S_IWUSR) != 0 ||
        listen(server->listen_fd, (int)STATD_IPC_MAX_CLIENTS) != 0) {
        const int saved_errno = errno;
        close(server->listen_fd);
        server->listen_fd = -1;
        unlink(socket_path);
        errno = saved_errno;
        return STATD_IPC_SYSTEM_ERROR;
    }

    memcpy(server->socket_path, socket_path, path_len + 1U);
    return STATD_IPC_OK;
}

void statd_ipc_server_destroy(struct statd_ipc_server *server)
{
    size_t index = 0U;
    if (server == NULL) {
        return;
    }
    for (index = 0U; index < STATD_IPC_MAX_CLIENTS; index++) {
        client_close(&server->clients[index]);
    }
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
    }
    if (server->socket_path[0] != '\0') {
        unlink(server->socket_path);
    }
    server->listen_fd = -1;
    server->socket_path[0] = '\0';
}

enum statd_ipc_status statd_ipc_server_step(struct statd_ipc_server *server, int timeout_ms)
{
    struct pollfd pollfds[STATD_IPC_MAX_CLIENTS + 1U];
    size_t slot_for_poll[STATD_IPC_MAX_CLIENTS + 1U];
    nfds_t count = 1U;
    size_t index = 0U;
    int poll_result = 0;

    if (server == NULL || server->listen_fd < 0 || server->registry == NULL || timeout_ms < -1) {
        return STATD_IPC_INVALID;
    }

    memset(pollfds, 0, sizeof(pollfds));
    pollfds[0].fd = server->listen_fd;
    pollfds[0].events = POLLIN;
    slot_for_poll[0] = STATD_IPC_MAX_CLIENTS;

    for (index = 0U; index < STATD_IPC_MAX_CLIENTS; index++) {
        struct statd_ipc_client *client = &server->clients[index];
        if (client->fd >= 0) {
            pollfds[count].fd = client->fd;
            pollfds[count].events = client->output_len > 0U ? POLLOUT : POLLIN;
            slot_for_poll[count] = index;
            count++;
        }
    }

    poll_result = poll(pollfds, count, timeout_ms);
    if (poll_result < 0) {
        return errno == EINTR ? STATD_IPC_OK : STATD_IPC_SYSTEM_ERROR;
    }
    if (poll_result == 0) {
        return STATD_IPC_OK;
    }

    if ((pollfds[0].revents & POLLIN) != 0) {
        accept_clients(server);
    }
    for (index = 1U; index < (size_t)count; index++) {
        struct statd_ipc_client *client = &server->clients[slot_for_poll[index]];
        const short revents = pollfds[index].revents;
        if (client->fd < 0) {
            continue;
        }
        if ((revents & (POLLERR | POLLNVAL)) != 0) {
            client_close(client);
            continue;
        }
        if ((revents & POLLIN) != 0 && client->output_len == 0U) {
            handle_read(server, client);
        }
        if (client->fd >= 0 && (revents & POLLOUT) != 0 && client->output_len > 0U) {
            handle_write(server, client);
        }
        if (client->fd >= 0 && (revents & POLLHUP) != 0 && (revents & POLLIN) == 0 &&
            client->output_len == 0U) {
            client_close(client);
        }
    }
    return STATD_IPC_OK;
}
