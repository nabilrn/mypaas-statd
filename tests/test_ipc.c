#include "ipc.h"
#include "sampler.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
            return 1;                                                                               \
        }                                                                                           \
    } while (0)

struct test_env {
    char base[256];
    char root[256];
    char group[256];
    char socket_path[256];
    struct statd_registry registry;
    struct statd_ipc_server server;
};

static int write_text(const char *directory, const char *name, const char *content)
{
    char path[512];
    FILE *file = NULL;
    const int written = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return -1;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    if (fwrite(content, 1U, strlen(content), file) != strlen(content)) {
        fclose(file);
        return -1;
    }
    return fclose(file);
}

static int env_init(struct test_env *env)
{
    char template_path[] = "/tmp/mypaas-statd-ipc-XXXXXX";
    char *base = mkdtemp(template_path);
    if (base == NULL) {
        return -1;
    }
    memset(env, 0, sizeof(*env));
    if (snprintf(env->base, sizeof(env->base), "%s", base) < 0 ||
        snprintf(env->root, sizeof(env->root), "%s/root", base) < 0 ||
        snprintf(env->group, sizeof(env->group), "%s/root/workload", base) < 0 ||
        snprintf(env->socket_path, sizeof(env->socket_path), "%s/statd.sock", base) < 0) {
        return -1;
    }
    if (mkdir(env->root, 0700) != 0 || mkdir(env->group, 0700) != 0) {
        return -1;
    }
    if (write_text(env->group, "cpu.stat", "usage_usec 1000\n") != 0 ||
        write_text(env->group, "cpu.max", "max 100000\n") != 0 ||
        write_text(env->group, "memory.current", "4096\n") != 0 ||
        write_text(env->group, "memory.max", "max\n") != 0 ||
        write_text(env->group, "memory.events", "oom 2\noom_kill 1\n") != 0 ||
        write_text(env->group, "pids.current", "7\n") != 0 ||
        write_text(env->group, "pids.max", "64\n") != 0) {
        return -1;
    }
    if (statd_registry_init(&env->registry, env->root) != STATD_SAMPLER_OK ||
        statd_ipc_server_init(&env->server, &env->registry, env->socket_path) != STATD_IPC_OK) {
        return -1;
    }
    return 0;
}

static void env_destroy(struct test_env *env)
{
    static const char *files[] = {"cpu.stat",       "cpu.max",       "memory.current",
                                  "memory.max",     "memory.events", "pids.current",
                                  "pids.max"};
    char path[512];
    size_t index = 0U;
    statd_ipc_server_destroy(&env->server);
    statd_registry_destroy(&env->registry);
    for (index = 0U; index < sizeof(files) / sizeof(files[0]); index++) {
        const int written = snprintf(path, sizeof(path), "%s/%s", env->group, files[index]);
        if (written >= 0 && (size_t)written < sizeof(path)) {
            unlink(path);
        }
    }
    rmdir(env->group);
    rmdir(env->root);
    rmdir(env->base);
}

static int connect_client(const char *path)
{
    struct sockaddr_un address;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    size_t path_len = strlen(path);
    if (fd < 0 || path_len >= sizeof(address.sun_path)) {
        if (fd >= 0) {
            close(fd);
        }
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, path_len + 1U);
    if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int pump(struct statd_ipc_server *server, int iterations)
{
    int index = 0;
    for (index = 0; index < iterations; index++) {
        CHECK(statd_ipc_server_step(server, 0) == STATD_IPC_OK);
    }
    return 0;
}

static int read_available(int fd, char *buffer, size_t capacity)
{
    ssize_t count = recv(fd, buffer, capacity - 1U, MSG_DONTWAIT);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }
    if (count <= 0) {
        return -1;
    }
    buffer[(size_t)count] = '\0';
    return (int)count;
}

static int test_permissions_and_hello(void)
{
    struct test_env env;
    struct stat st;
    char response[1024];
    int client = -1;

    CHECK(env_init(&env) == 0);
    CHECK(stat(env.socket_path, &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);
    client = connect_client(env.socket_path);
    CHECK(client >= 0);
    CHECK(send(client, "{\"op\":\"hello\",\"protocol\":1}\n", 28U, 0) == 28);
    CHECK(pump(&env.server, 4) == 0);
    CHECK(read_available(client, response, sizeof(response)) > 0);
    CHECK(strstr(response, "\"ok\":true") != NULL);
    CHECK(strstr(response, "\"protocol\":1") != NULL);
    close(client);
    env_destroy(&env);
    CHECK(access(env.socket_path, F_OK) != 0);
    return 0;
}

static int test_fragmented_register_and_snapshot(void)
{
    struct test_env env;
    char response[4096];
    int client = -1;
    const char *hello = "{\"op\":\"hello\",\"protocol\":1}\n";
    const char *part1 = "{\"op\":\"register\",\"id\":\"runtime-1\",";
    const char *part2 = "\"cgroup\":\"workload\"}\n";
    const char *snapshot = "{\"op\":\"snapshot\",\"id\":\"runtime-1\"}\n";

    CHECK(env_init(&env) == 0);
    client = connect_client(env.socket_path);
    CHECK(client >= 0);
    CHECK(send(client, hello, strlen(hello), 0) == (ssize_t)strlen(hello));
    CHECK(pump(&env.server, 4) == 0);
    CHECK(read_available(client, response, sizeof(response)) > 0);

    CHECK(send(client, part1, strlen(part1), 0) == (ssize_t)strlen(part1));
    CHECK(pump(&env.server, 2) == 0);
    CHECK(read_available(client, response, sizeof(response)) == 0);
    CHECK(send(client, part2, strlen(part2), 0) == (ssize_t)strlen(part2));
    CHECK(pump(&env.server, 4) == 0);
    CHECK(read_available(client, response, sizeof(response)) > 0);
    CHECK(strstr(response, "\"ok\":true") != NULL);

    CHECK(send(client, snapshot, strlen(snapshot), 0) == (ssize_t)strlen(snapshot));
    CHECK(pump(&env.server, 4) == 0);
    CHECK(read_available(client, response, sizeof(response)) > 0);
    CHECK(strstr(response, "\"valid\":true") != NULL);
    CHECK(strstr(response, "\"current_bytes\":4096") != NULL);
    CHECK(strstr(response, "\"max_bytes\":null") != NULL);
    close(client);
    env_destroy(&env);
    return 0;
}

static int test_multiple_messages_and_errors(void)
{
    struct test_env env;
    char response[4096];
    int client = -1;
    const char *batch = "{\"op\":\"hello\",\"protocol\":1}\n{\"op\":\"status\"}\n";
    const char *bad = "{\"op\":\"status\",\"extra\":1}\n";

    CHECK(env_init(&env) == 0);
    client = connect_client(env.socket_path);
    CHECK(client >= 0);
    CHECK(send(client, batch, strlen(batch), 0) == (ssize_t)strlen(batch));
    CHECK(pump(&env.server, 8) == 0);
    CHECK(read_available(client, response, sizeof(response)) > 0);
    CHECK(strstr(response, "\"agent\":\"mypaas-statd\"") != NULL);
    CHECK(strstr(response, "\"registrations\":0") != NULL);

    CHECK(send(client, bad, strlen(bad), 0) == (ssize_t)strlen(bad));
    CHECK(pump(&env.server, 4) == 0);
    CHECK(read_available(client, response, sizeof(response)) > 0);
    CHECK(strstr(response, "INVALID_REQUEST") != NULL);
    close(client);
    env_destroy(&env);
    return 0;
}

static int test_protocol_and_handshake_errors(void)
{
    struct test_env env;
    char response[1024];
    int client = -1;
    const char *before_hello = "{\"op\":\"status\"}\n";
    const char *wrong_version = "{\"op\":\"hello\",\"protocol\":2}\n";

    CHECK(env_init(&env) == 0);
    client = connect_client(env.socket_path);
    CHECK(client >= 0);
    CHECK(send(client, before_hello, strlen(before_hello), 0) == (ssize_t)strlen(before_hello));
    CHECK(pump(&env.server, 4) == 0);
    CHECK(read_available(client, response, sizeof(response)) > 0);
    CHECK(strstr(response, "HELLO_REQUIRED") != NULL);
    CHECK(send(client, wrong_version, strlen(wrong_version), 0) == (ssize_t)strlen(wrong_version));
    CHECK(pump(&env.server, 4) == 0);
    CHECK(read_available(client, response, sizeof(response)) > 0);
    CHECK(strstr(response, "UNSUPPORTED_PROTOCOL") != NULL);
    close(client);
    env_destroy(&env);
    return 0;
}

static int test_oversized_and_broken_client_isolation(void)
{
    struct test_env env;
    char response[1024];
    char *oversized = NULL;
    int bad_client = -1;
    int good_client = -1;
    size_t index = 0U;
    const char *hello = "{\"op\":\"hello\",\"protocol\":1}\n";

    CHECK(env_init(&env) == 0);
    bad_client = connect_client(env.socket_path);
    CHECK(bad_client >= 0);
    oversized = malloc(STATD_IPC_MESSAGE_MAX + 32U);
    CHECK(oversized != NULL);
    for (index = 0U; index < STATD_IPC_MESSAGE_MAX + 31U; index++) {
        oversized[index] = 'x';
    }
    CHECK(send(bad_client, oversized, STATD_IPC_MESSAGE_MAX + 31U, 0) > 0);
    free(oversized);
    CHECK(pump(&env.server, 8) == 0);
    CHECK(read_available(bad_client, response, sizeof(response)) > 0);
    CHECK(strstr(response, "MESSAGE_TOO_LARGE") != NULL);
    close(bad_client);

    good_client = connect_client(env.socket_path);
    CHECK(good_client >= 0);
    CHECK(send(good_client, hello, strlen(hello), 0) == (ssize_t)strlen(hello));
    CHECK(pump(&env.server, 4) == 0);
    CHECK(read_available(good_client, response, sizeof(response)) > 0);
    CHECK(strstr(response, "\"ok\":true") != NULL);
    close(good_client);
    env_destroy(&env);
    return 0;
}

static int test_disconnect_does_not_stop_server(void)
{
    struct test_env env;
    char response[1024];
    int first = -1;
    int second = -1;
    const char *hello = "{\"op\":\"hello\",\"protocol\":1}\n";

    CHECK(env_init(&env) == 0);
    first = connect_client(env.socket_path);
    CHECK(first >= 0);
    CHECK(pump(&env.server, 2) == 0);
    close(first);
    CHECK(pump(&env.server, 2) == 0);

    second = connect_client(env.socket_path);
    CHECK(second >= 0);
    CHECK(send(second, hello, strlen(hello), 0) == (ssize_t)strlen(hello));
    CHECK(pump(&env.server, 4) == 0);
    CHECK(read_available(second, response, sizeof(response)) > 0);
    CHECK(strstr(response, "\"ok\":true") != NULL);
    close(second);
    env_destroy(&env);
    return 0;
}

int main(void)
{
    CHECK(test_permissions_and_hello() == 0);
    CHECK(test_fragmented_register_and_snapshot() == 0);
    CHECK(test_multiple_messages_and_errors() == 0);
    CHECK(test_protocol_and_handshake_errors() == 0);
    CHECK(test_oversized_and_broken_client_isolation() == 0);
    CHECK(test_disconnect_does_not_stop_server() == 0);
    puts("phase 3 unix socket protocol tests passed");
    return 0;
}
