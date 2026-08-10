#ifndef MYPAAS_STATD_IPC_H
#define MYPAAS_STATD_IPC_H

#include "proc_cgroup.h"
#include "sampler.h"

#include <stdbool.h>
#include <stddef.h>

#define STATD_PROTOCOL_VERSION 1U
#define STATD_IPC_MAX_CLIENTS 8U
#define STATD_IPC_MESSAGE_MAX 8192U
#define STATD_IPC_OUTPUT_MAX 4096U
#define STATD_IPC_SOCKET_PATH_MAX 107U

struct statd_ipc_client {
    int fd;
    bool negotiated;
    bool close_after_write;
    char input[STATD_IPC_MESSAGE_MAX];
    size_t input_len;
    char output[STATD_IPC_OUTPUT_MAX];
    size_t output_len;
    size_t output_offset;
};

struct statd_ipc_server {
    int listen_fd;
    char socket_path[STATD_IPC_SOCKET_PATH_MAX + 1U];
    char proc_root[STATD_PROC_ROOT_MAX + 1U];
    struct statd_registry *registry;
    struct statd_ipc_client clients[STATD_IPC_MAX_CLIENTS];
};

enum statd_ipc_status {
    STATD_IPC_OK = 0,
    STATD_IPC_INVALID,
    STATD_IPC_SYSTEM_ERROR
};

enum statd_ipc_status statd_ipc_server_init(struct statd_ipc_server *server,
                                              struct statd_registry *registry,
                                              const char *socket_path,
                                              const char *proc_root);
void statd_ipc_server_destroy(struct statd_ipc_server *server);
enum statd_ipc_status statd_ipc_server_step(struct statd_ipc_server *server, int timeout_ms);

#endif
