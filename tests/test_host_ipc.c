#include "ipc.h"
#include "sampler.h"

#include <errno.h>
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
    char socket_path[256];
    struct statd_registry registry;
    struct statd_ipc_server server;
};

static int env_init(struct test_env *env)
{
    char template_path[] = "/tmp/mypaas-statd-host-ipc-XXXXXX";
    char *base = mkdtemp(template_path);
    if (base == NULL) {
        return -1;
    }
    memset(env, 0, sizeof(*env));
    if (snprintf(env->base, sizeof(env->base), "%s", base) < 0 ||
        snprintf(env->root, sizeof(env->root), "%s/root", base) < 0 ||
        snprintf(env->socket_path, sizeof(env->socket_path), "%s/statd.sock", base) < 0) {
        return -1;
    }
    if (mkdir(env->root, 0700) != 0) {
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
    statd_ipc_server_destroy(&env->server);
    statd_registry_destroy(&env->registry);
    rmdir(env->root);
    rmdir(env->base);
}

static int connect_client(const char *path)
{
    struct sockaddr_un address;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    const size_t path_len = strlen(path);
    if (fd < 0 || path_len >= sizeof(address.sun_path)) {
        if (fd >= 0) close(fd);
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
    const ssize_t count = recv(fd, buffer, capacity - 1U, MSG_DONTWAIT);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }
    if (count <= 0) {
        return -1;
    }
    buffer[(size_t)count] = '\0';
    return (int)count;
}

static int exchange(struct statd_ipc_server *server, int client, const char *message,
                    char *response, size_t response_capacity)
{
    CHECK(send(client, message, strlen(message), 0) == (ssize_t)strlen(message));
    CHECK(pump(server, 6) == 0);
    CHECK(read_available(client, response, response_capacity) > 0);
    return 0;
}

static int negotiate(struct statd_ipc_server *server, int client, char *response,
                     size_t response_capacity)
{
    CHECK(exchange(server, client, "{\"op\":\"hello\",\"protocol\":1}\n", response,
                   response_capacity) == 0);
    CHECK(strstr(response, "\"ok\":true") != NULL);
    CHECK(strstr(response, "\"protocol\":1") != NULL);
    return 0;
}

static int test_unavailable_then_complete_snapshot(void)
{
    struct test_env env;
    struct statd_host_snapshot snapshot = {0};
    char response[2048];
    int client = -1;

    CHECK(env_init(&env) == 0);
    client = connect_client(env.socket_path);
    CHECK(client >= 0);
    CHECK(negotiate(&env.server, client, response, sizeof(response)) == 0);

    CHECK(exchange(&env.server, client, "{\"op\":\"host_snapshot\"}\n", response,
                   sizeof(response)) == 0);
    CHECK(strstr(response, "HOST_METRICS_UNAVAILABLE") != NULL);

    snapshot.storage.valid = true;
    snapshot.storage.total_bytes = UINT64_C(1000);
    snapshot.storage.available_bytes = UINT64_C(400);
    snapshot.network.valid = true;
    CHECK(snprintf(snapshot.network.interface, sizeof(snapshot.network.interface), "%s", "eth0") > 0);
    snapshot.network.rx_bytes = UINT64_C(123);
    snapshot.network.tx_bytes = UINT64_C(456);
    statd_ipc_server_set_host_snapshot(&env.server, &snapshot);

    CHECK(exchange(&env.server, client, "{\"op\":\"host_snapshot\"}\n", response,
                   sizeof(response)) == 0);
    CHECK(strstr(response, "\"ok\":true") != NULL);
    CHECK(strstr(response, "\"total_bytes\":1000") != NULL);
    CHECK(strstr(response, "\"available_bytes\":400") != NULL);
    CHECK(strstr(response, "\"interface\":\"eth0\"") != NULL);
    CHECK(strstr(response, "\"rx_bytes\":123") != NULL);
    CHECK(strstr(response, "\"tx_bytes\":456") != NULL);

    close(client);
    env_destroy(&env);
    return 0;
}

static int test_partial_snapshot_serializes_null(void)
{
    struct test_env env;
    struct statd_host_snapshot snapshot = {0};
    char response[2048];
    int client = -1;

    CHECK(env_init(&env) == 0);
    snapshot.storage.valid = true;
    snapshot.storage.total_bytes = UINT64_C(4096);
    snapshot.storage.available_bytes = UINT64_C(1024);
    statd_ipc_server_set_host_snapshot(&env.server, &snapshot);

    client = connect_client(env.socket_path);
    CHECK(client >= 0);
    CHECK(negotiate(&env.server, client, response, sizeof(response)) == 0);
    CHECK(exchange(&env.server, client, "{\"op\":\"host_snapshot\"}\n", response,
                   sizeof(response)) == 0);
    CHECK(strstr(response, "\"storage\":{") != NULL);
    CHECK(strstr(response, "\"network\":null") != NULL);

    close(client);
    env_destroy(&env);
    return 0;
}

static int test_unsafe_interface_is_not_serialized(void)
{
    struct test_env env;
    struct statd_host_snapshot snapshot = {0};
    char response[2048];
    int client = -1;

    CHECK(env_init(&env) == 0);
    snapshot.network.valid = true;
    CHECK(snprintf(snapshot.network.interface, sizeof(snapshot.network.interface), "%s", "bad\\iface") > 0);
    snapshot.network.rx_bytes = UINT64_C(10);
    snapshot.network.tx_bytes = UINT64_C(20);
    statd_ipc_server_set_host_snapshot(&env.server, &snapshot);

    client = connect_client(env.socket_path);
    CHECK(client >= 0);
    CHECK(negotiate(&env.server, client, response, sizeof(response)) == 0);
    CHECK(exchange(&env.server, client, "{\"op\":\"host_snapshot\"}\n", response,
                   sizeof(response)) == 0);
    CHECK(strstr(response, "\"storage\":null") != NULL);
    CHECK(strstr(response, "\"network\":null") != NULL);
    CHECK(strstr(response, "bad\\iface") == NULL);

    close(client);
    env_destroy(&env);
    return 0;
}

int main(void)
{
    CHECK(test_unavailable_then_complete_snapshot() == 0);
    CHECK(test_partial_snapshot_serializes_null() == 0);
    CHECK(test_unsafe_interface_is_not_serialized() == 0);
    puts("phase 6 host snapshot IPC tests passed");
    return 0;
}
