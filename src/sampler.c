#include "sampler.h"

#include <limits.h>
#include <string.h>
#include <unistd.h>

static bool valid_timespec(struct timespec value)
{
    return value.tv_sec >= 0 && value.tv_nsec >= 0 && value.tv_nsec < 1000000000L;
}

static bool elapsed_nanoseconds(struct timespec previous, struct timespec current,
                                uint64_t *out_nanoseconds)
{
    time_t seconds = 0;
    long nanoseconds = 0L;
    uint64_t seconds_u64 = 0U;

    if (out_nanoseconds == NULL || !valid_timespec(previous) || !valid_timespec(current) ||
        current.tv_sec < previous.tv_sec ||
        (current.tv_sec == previous.tv_sec && current.tv_nsec < previous.tv_nsec)) {
        return false;
    }

    seconds = current.tv_sec - previous.tv_sec;
    nanoseconds = current.tv_nsec - previous.tv_nsec;
    if (nanoseconds < 0L) {
        if (seconds == 0) {
            return false;
        }
        seconds--;
        nanoseconds += 1000000000L;
    }
    seconds_u64 = (uint64_t)seconds;
    if (seconds_u64 > UINT64_MAX / UINT64_C(1000000000)) {
        return false;
    }
    *out_nanoseconds = seconds_u64 * UINT64_C(1000000000) + (uint64_t)nanoseconds;
    return true;
}

void statd_sample_state_init(struct statd_sample_state *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

enum statd_sampler_status statd_sample_apply(struct statd_sample_state *state,
                                              const struct statd_raw_snapshot *raw,
                                              struct timespec now,
                                              struct statd_snapshot *out)
{
    struct statd_snapshot next = {0};
    uint64_t elapsed_ns = 0U;

    if (state == NULL || raw == NULL || out == NULL || !valid_timespec(now)) {
        return STATD_SAMPLER_INVALID;
    }

    next.sampled_at = now;
    next.cpu_usage_usec = raw->cpu.usage_usec;
    next.cpu_max = raw->cpu_max;
    next.memory_current_bytes = raw->memory_current_bytes;
    next.memory_max = raw->memory_max;
    next.memory_events = raw->memory_events;
    next.pids_current = raw->pids_current;
    next.pids_max = raw->pids_max;

    if (state->has_cpu_baseline && raw->cpu.usage_usec >= state->previous_cpu_usage_usec &&
        elapsed_nanoseconds(state->previous_sample_time, now, &elapsed_ns) && elapsed_ns > 0U) {
        const uint64_t usage_delta = raw->cpu.usage_usec - state->previous_cpu_usage_usec;
        next.cpu_percent = ((double)usage_delta * 100000.0) / (double)elapsed_ns;
        next.cpu_percent_valid = true;
    }

    state->has_cpu_baseline = true;
    state->previous_cpu_usage_usec = raw->cpu.usage_usec;
    state->previous_sample_time = now;
    *out = next;
    return STATD_SAMPLER_OK;
}

enum statd_sampler_status statd_sample_once(int cgroup_fd, struct statd_sample_state *state,
                                             struct statd_snapshot *out)
{
    struct statd_raw_snapshot raw = {0};
    struct timespec now = {0};

    if (cgroup_fd < 0 || state == NULL || out == NULL) {
        return STATD_SAMPLER_INVALID;
    }
    {
        const enum statd_cgroup_status read_status = statd_cgroup_read_snapshot(cgroup_fd, &raw);
        if (read_status == STATD_CGROUP_NOT_FOUND) {
            return STATD_SAMPLER_NOT_FOUND;
        }
        if (read_status != STATD_CGROUP_OK) {
            return STATD_SAMPLER_CGROUP_ERROR;
        }
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return STATD_SAMPLER_CLOCK_ERROR;
    }
    return statd_sample_apply(state, &raw, now, out);
}

static struct statd_registration *find_registration(struct statd_registry *registry,
                                                     const char *id)
{
    size_t index = 0U;
    if (registry == NULL || id == NULL) {
        return NULL;
    }
    for (index = 0U; index < STATD_MAX_REGISTRATIONS; index++) {
        if (registry->registrations[index].used &&
            strcmp(registry->registrations[index].id, id) == 0) {
            return &registry->registrations[index];
        }
    }
    return NULL;
}

static const struct statd_registration *find_registration_const(
    const struct statd_registry *registry, const char *id)
{
    size_t index = 0U;
    if (registry == NULL || id == NULL) {
        return NULL;
    }
    for (index = 0U; index < STATD_MAX_REGISTRATIONS; index++) {
        if (registry->registrations[index].used &&
            strcmp(registry->registrations[index].id, id) == 0) {
            return &registry->registrations[index];
        }
    }
    return NULL;
}

enum statd_sampler_status statd_registry_init(struct statd_registry *registry,
                                               const char *cgroup_root)
{
    size_t root_len = 0U;
    if (registry == NULL || cgroup_root == NULL) {
        return STATD_SAMPLER_INVALID;
    }
    root_len = strnlen(cgroup_root, STATD_CGROUP_PATH_MAX + 1U);
    if (root_len == 0U || root_len > STATD_CGROUP_PATH_MAX) {
        return STATD_SAMPLER_INVALID;
    }
    memset(registry, 0, sizeof(*registry));
    memcpy(registry->cgroup_root, cgroup_root, root_len);
    registry->cgroup_root[root_len] = '\0';
    return STATD_SAMPLER_OK;
}

void statd_registry_destroy(struct statd_registry *registry)
{
    size_t index = 0U;
    if (registry == NULL) {
        return;
    }
    for (index = 0U; index < STATD_MAX_REGISTRATIONS; index++) {
        if (registry->registrations[index].used) {
            close(registry->registrations[index].cgroup_fd);
        }
    }
    memset(registry, 0, sizeof(*registry));
}

enum statd_sampler_status statd_registry_register(struct statd_registry *registry, const char *id,
                                                   const char *relative_cgroup_path)
{
    size_t id_len = 0U;
    size_t index = 0U;
    int cgroup_fd = -1;
    enum statd_cgroup_status open_status = STATD_CGROUP_OK;

    if (registry == NULL || id == NULL || relative_cgroup_path == NULL) {
        return STATD_SAMPLER_INVALID;
    }
    id_len = strnlen(id, STATD_RUNTIME_ID_MAX + 1U);
    if (id_len == 0U || id_len > STATD_RUNTIME_ID_MAX) {
        return STATD_SAMPLER_INVALID;
    }
    if (find_registration(registry, id) != NULL) {
        return STATD_SAMPLER_INVALID;
    }
    if (registry->count >= STATD_MAX_REGISTRATIONS) {
        return STATD_SAMPLER_LIMIT;
    }

    open_status = statd_cgroup_open(registry->cgroup_root, relative_cgroup_path, &cgroup_fd);
    if (open_status == STATD_CGROUP_NOT_FOUND) {
        return STATD_SAMPLER_NOT_FOUND;
    }
    if (open_status == STATD_CGROUP_INVALID) {
        return STATD_SAMPLER_INVALID;
    }
    if (open_status != STATD_CGROUP_OK) {
        return STATD_SAMPLER_CGROUP_ERROR;
    }

    for (index = 0U; index < STATD_MAX_REGISTRATIONS; index++) {
        struct statd_registration *registration = &registry->registrations[index];
        if (!registration->used) {
            memset(registration, 0, sizeof(*registration));
            registration->used = true;
            registration->cgroup_fd = cgroup_fd;
            registration->last_sample_status = STATD_SAMPLER_NOT_FOUND;
            memcpy(registration->id, id, id_len);
            registration->id[id_len] = '\0';
            statd_sample_state_init(&registration->state);
            registry->count++;
            return STATD_SAMPLER_OK;
        }
    }

    close(cgroup_fd);
    return STATD_SAMPLER_LIMIT;
}

enum statd_sampler_status statd_registry_unregister(struct statd_registry *registry,
                                                     const char *id)
{
    struct statd_registration *registration = NULL;
    if (registry == NULL || id == NULL) {
        return STATD_SAMPLER_INVALID;
    }
    registration = find_registration(registry, id);
    if (registration == NULL) {
        return STATD_SAMPLER_OK;
    }
    close(registration->cgroup_fd);
    memset(registration, 0, sizeof(*registration));
    registry->count--;
    return STATD_SAMPLER_OK;
}

enum statd_sampler_status statd_registry_sample(struct statd_registry *registry, const char *id,
                                                 struct statd_snapshot *out)
{
    struct statd_registration *registration = NULL;
    struct statd_snapshot next = {0};
    enum statd_sampler_status status = STATD_SAMPLER_OK;

    if (registry == NULL || id == NULL || out == NULL) {
        return STATD_SAMPLER_INVALID;
    }
    registration = find_registration(registry, id);
    if (registration == NULL) {
        return STATD_SAMPLER_NOT_FOUND;
    }
    status = statd_sample_once(registration->cgroup_fd, &registration->state, &next);
    registration->last_sample_status = status;
    if (status != STATD_SAMPLER_OK) {
        return status;
    }
    registration->latest = next;
    registration->has_snapshot = true;
    *out = next;
    return STATD_SAMPLER_OK;
}

void statd_registry_sample_all(struct statd_registry *registry)
{
    size_t index = 0U;
    if (registry == NULL) {
        return;
    }
    for (index = 0U; index < STATD_MAX_REGISTRATIONS; index++) {
        struct statd_registration *registration = &registry->registrations[index];
        if (registration->used) {
            struct statd_snapshot ignored = {0};
            (void)statd_registry_sample(registry, registration->id, &ignored);
        }
    }
}

enum statd_sampler_status statd_registry_latest(const struct statd_registry *registry,
                                                 const char *id, struct statd_snapshot *out)
{
    const struct statd_registration *registration = NULL;
    if (registry == NULL || id == NULL || out == NULL) {
        return STATD_SAMPLER_INVALID;
    }
    registration = find_registration_const(registry, id);
    if (registration == NULL || !registration->has_snapshot) {
        return STATD_SAMPLER_NOT_FOUND;
    }
    *out = registration->latest;
    return STATD_SAMPLER_OK;
}

enum statd_sampler_status statd_registry_runtime_state(const struct statd_registry *registry,
                                                        const char *id, bool *out_has_snapshot,
                                                        enum statd_sampler_status *out_last_status)
{
    const struct statd_registration *registration = NULL;
    if (registry == NULL || id == NULL || out_has_snapshot == NULL || out_last_status == NULL) {
        return STATD_SAMPLER_INVALID;
    }
    registration = find_registration_const(registry, id);
    if (registration == NULL) {
        return STATD_SAMPLER_NOT_FOUND;
    }
    *out_has_snapshot = registration->has_snapshot;
    *out_last_status = registration->last_sample_status;
    return STATD_SAMPLER_OK;
}
