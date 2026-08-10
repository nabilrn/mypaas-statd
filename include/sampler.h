#ifndef MYPAAS_STATD_SAMPLER_H
#define MYPAAS_STATD_SAMPLER_H

#include "cgroup_reader.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define STATD_MAX_REGISTRATIONS 64U
#define STATD_RUNTIME_ID_MAX 127U

struct statd_snapshot {
    struct timespec sampled_at;
    uint64_t cpu_usage_usec;
    bool cpu_percent_valid;
    double cpu_percent;
    struct statd_cpu_max cpu_max;
    uint64_t memory_current_bytes;
    struct statd_limit memory_max;
    struct statd_memory_events memory_events;
    uint64_t pids_current;
    struct statd_limit pids_max;
};

struct statd_sample_state {
    bool has_cpu_baseline;
    uint64_t previous_cpu_usage_usec;
    struct timespec previous_sample_time;
};

enum statd_sampler_status {
    STATD_SAMPLER_OK = 0,
    STATD_SAMPLER_INVALID,
    STATD_SAMPLER_NOT_FOUND,
    STATD_SAMPLER_LIMIT,
    STATD_SAMPLER_CGROUP_ERROR,
    STATD_SAMPLER_CLOCK_ERROR
};

struct statd_registration {
    bool used;
    char id[STATD_RUNTIME_ID_MAX + 1U];
    int cgroup_fd;
    struct statd_sample_state state;
    bool has_snapshot;
    struct statd_snapshot latest;
};

struct statd_registry {
    char cgroup_root[STATD_CGROUP_PATH_MAX + 1U];
    size_t count;
    struct statd_registration registrations[STATD_MAX_REGISTRATIONS];
};

void statd_sample_state_init(struct statd_sample_state *state);
enum statd_sampler_status statd_sample_apply(struct statd_sample_state *state,
                                              const struct statd_raw_snapshot *raw,
                                              struct timespec now,
                                              struct statd_snapshot *out);
enum statd_sampler_status statd_sample_once(int cgroup_fd, struct statd_sample_state *state,
                                             struct statd_snapshot *out);

enum statd_sampler_status statd_registry_init(struct statd_registry *registry,
                                               const char *cgroup_root);
void statd_registry_destroy(struct statd_registry *registry);
enum statd_sampler_status statd_registry_register(struct statd_registry *registry, const char *id,
                                                   const char *relative_cgroup_path);
enum statd_sampler_status statd_registry_unregister(struct statd_registry *registry,
                                                     const char *id);
enum statd_sampler_status statd_registry_sample(struct statd_registry *registry, const char *id,
                                                 struct statd_snapshot *out);
enum statd_sampler_status statd_registry_latest(const struct statd_registry *registry,
                                                 const char *id, struct statd_snapshot *out);

#endif
