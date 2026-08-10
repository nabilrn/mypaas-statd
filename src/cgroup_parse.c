#include "cgroup_parse.h"

#include <limits.h>
#include <string.h>

struct span {
    const char *ptr;
    size_t len;
};

static bool is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

static struct span trim(struct span value)
{
    while (value.len > 0U && is_space(value.ptr[0])) {
        value.ptr++;
        value.len--;
    }
    while (value.len > 0U && is_space(value.ptr[value.len - 1U])) {
        value.len--;
    }
    return value;
}

static bool span_equal(struct span value, const char *literal)
{
    const size_t literal_len = strlen(literal);
    return value.len == literal_len && memcmp(value.ptr, literal, literal_len) == 0;
}

static enum statd_parse_status parse_u64(struct span value, uint64_t *out)
{
    uint64_t result = 0U;
    size_t index = 0U;

    if (out == NULL) {
        return STATD_PARSE_INVALID;
    }
    value = trim(value);
    if (value.len == 0U) {
        return STATD_PARSE_INVALID;
    }
    for (index = 0U; index < value.len; index++) {
        const unsigned char ch = (unsigned char)value.ptr[index];
        uint64_t digit = 0U;
        if (ch < (unsigned char)'0' || ch > (unsigned char)'9') {
            return STATD_PARSE_INVALID;
        }
        digit = (uint64_t)(ch - (unsigned char)'0');
        if (result > (UINT64_MAX - digit) / UINT64_C(10)) {
            return STATD_PARSE_RANGE;
        }
        result = result * UINT64_C(10) + digit;
    }
    *out = result;
    return STATD_PARSE_OK;
}

static enum statd_parse_status next_token(struct span input, size_t *offset, struct span *token)
{
    size_t cursor = 0U;
    size_t start = 0U;

    if (offset == NULL || token == NULL || *offset > input.len) {
        return STATD_PARSE_INVALID;
    }
    cursor = *offset;
    while (cursor < input.len && is_space(input.ptr[cursor])) {
        cursor++;
    }
    if (cursor == input.len) {
        *offset = cursor;
        token->ptr = input.ptr + cursor;
        token->len = 0U;
        return STATD_PARSE_MISSING;
    }
    start = cursor;
    while (cursor < input.len && !is_space(input.ptr[cursor])) {
        cursor++;
    }
    token->ptr = input.ptr + start;
    token->len = cursor - start;
    *offset = cursor;
    return STATD_PARSE_OK;
}

static bool only_space_remaining(struct span input, size_t offset)
{
    while (offset < input.len) {
        if (!is_space(input.ptr[offset])) {
            return false;
        }
        offset++;
    }
    return true;
}

static enum statd_parse_status parse_single_u64(const char *input, size_t len, uint64_t *out)
{
    struct span value = {input, len};
    if (input == NULL || out == NULL) {
        return STATD_PARSE_INVALID;
    }
    return parse_u64(value, out);
}

static enum statd_parse_status parse_limit(const char *input, size_t len, struct statd_limit *out)
{
    struct span value = {input, len};
    uint64_t parsed = 0U;
    enum statd_parse_status status = STATD_PARSE_OK;

    if (input == NULL || out == NULL) {
        return STATD_PARSE_INVALID;
    }
    value = trim(value);
    if (span_equal(value, "max")) {
        out->unlimited = true;
        out->value = 0U;
        return STATD_PARSE_OK;
    }
    status = parse_u64(value, &parsed);
    if (status != STATD_PARSE_OK) {
        return status;
    }
    out->unlimited = false;
    out->value = parsed;
    return STATD_PARSE_OK;
}

static enum statd_parse_status parse_required_key_u64(const char *input, size_t len,
                                                        const char *required_key,
                                                        uint64_t *out_value)
{
    size_t offset = 0U;
    bool found = false;
    struct span all = {input, len};

    if (input == NULL || required_key == NULL || out_value == NULL) {
        return STATD_PARSE_INVALID;
    }

    while (offset < len) {
        size_t line_end = offset;
        size_t token_offset = 0U;
        struct span line = {0};
        struct span key = {0};
        struct span value = {0};
        enum statd_parse_status status = STATD_PARSE_OK;

        while (line_end < len && all.ptr[line_end] != '\n') {
            line_end++;
        }
        line.ptr = all.ptr + offset;
        line.len = line_end - offset;
        line = trim(line);
        offset = line_end < len ? line_end + 1U : line_end;
        if (line.len == 0U) {
            continue;
        }

        status = next_token(line, &token_offset, &key);
        if (status != STATD_PARSE_OK) {
            return STATD_PARSE_INVALID;
        }
        status = next_token(line, &token_offset, &value);
        if (status != STATD_PARSE_OK || !only_space_remaining(line, token_offset)) {
            return STATD_PARSE_INVALID;
        }

        if (span_equal(key, required_key)) {
            if (found) {
                return STATD_PARSE_INVALID;
            }
            status = parse_u64(value, out_value);
            if (status != STATD_PARSE_OK) {
                return status;
            }
            found = true;
        }
    }

    return found ? STATD_PARSE_OK : STATD_PARSE_MISSING;
}

enum statd_parse_status statd_parse_cpu_stat(const char *input, size_t len,
                                               struct statd_cpu_stat *out)
{
    struct statd_cpu_stat parsed = {0};
    enum statd_parse_status status = STATD_PARSE_OK;
    if (out == NULL) {
        return STATD_PARSE_INVALID;
    }
    status = parse_required_key_u64(input, len, "usage_usec", &parsed.usage_usec);
    if (status == STATD_PARSE_OK) {
        *out = parsed;
    }
    return status;
}

enum statd_parse_status statd_parse_cpu_max(const char *input, size_t len,
                                              struct statd_cpu_max *out)
{
    struct span all = {input, len};
    struct span quota = {0};
    struct span period = {0};
    struct statd_cpu_max parsed = {0};
    size_t offset = 0U;
    enum statd_parse_status status = STATD_PARSE_OK;

    if (input == NULL || out == NULL) {
        return STATD_PARSE_INVALID;
    }
    status = next_token(all, &offset, &quota);
    if (status != STATD_PARSE_OK) {
        return STATD_PARSE_INVALID;
    }
    status = next_token(all, &offset, &period);
    if (status != STATD_PARSE_OK || !only_space_remaining(all, offset)) {
        return STATD_PARSE_INVALID;
    }

    if (span_equal(quota, "max")) {
        parsed.quota_usec.unlimited = true;
        parsed.quota_usec.value = 0U;
    } else {
        status = parse_u64(quota, &parsed.quota_usec.value);
        if (status != STATD_PARSE_OK) {
            return status;
        }
        parsed.quota_usec.unlimited = false;
    }
    status = parse_u64(period, &parsed.period_usec);
    if (status != STATD_PARSE_OK) {
        return status;
    }
    *out = parsed;
    return STATD_PARSE_OK;
}

enum statd_parse_status statd_parse_memory_current(const char *input, size_t len,
                                                     uint64_t *out_bytes)
{
    return parse_single_u64(input, len, out_bytes);
}

enum statd_parse_status statd_parse_memory_max(const char *input, size_t len,
                                                 struct statd_limit *out)
{
    return parse_limit(input, len, out);
}

enum statd_parse_status statd_parse_memory_events(const char *input, size_t len,
                                                    struct statd_memory_events *out)
{
    struct statd_memory_events parsed = {0};
    enum statd_parse_status status = STATD_PARSE_OK;

    if (out == NULL) {
        return STATD_PARSE_INVALID;
    }
    status = parse_required_key_u64(input, len, "oom", &parsed.oom);
    if (status != STATD_PARSE_OK) {
        return status;
    }
    status = parse_required_key_u64(input, len, "oom_kill", &parsed.oom_kill);
    if (status != STATD_PARSE_OK) {
        return status;
    }
    *out = parsed;
    return STATD_PARSE_OK;
}

enum statd_parse_status statd_parse_pids_current(const char *input, size_t len,
                                                   uint64_t *out_count)
{
    return parse_single_u64(input, len, out_count);
}

enum statd_parse_status statd_parse_pids_max(const char *input, size_t len,
                                               struct statd_limit *out)
{
    return parse_limit(input, len, out);
}
