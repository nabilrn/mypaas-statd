# Skill: mypaas-c

Use this skill for C implementation and review in `mypaas-statd`.

## Objective

Produce boring, explicit, memory-safe-by-discipline C17 suitable for a small privileged Linux daemon. Prefer clarity and bounded behavior over clever abstractions.

## Required workflow

1. Read `AGENTS.md` and the relevant contract under `docs/`.
2. Identify ownership/lifetime for every object touched.
3. Identify all file descriptors acquired/released.
4. Define input bounds before parsing or allocation.
5. Implement the smallest change that satisfies the contract.
6. Add tests/fixtures before optimizing.
7. Run compiler warnings and sanitizers.

## Memory ownership

At each API boundary it must be obvious whether a pointer is:
- borrowed immutable;
- borrowed mutable;
- owned by caller;
- transferred to callee;
- returned newly allocated.

Prefer caller-owned fixed structs and bounded buffers in hot paths. Heap allocation is allowed when justified; it is not forbidden.

For `realloc`, never overwrite the original pointer until success is known.

## Strings and byte buffers

- `read`, `recv`, and kernel pseudo-files are byte sequences; add terminators only when capacity permits.
- Keep `(pointer, length, capacity)` reasoning explicit.
- Do not rely on locale-dependent parsing.
- Reject truncation when truncation changes identifiers, paths, numeric values, or protocol messages.
- Use `strtoull`/`strtoll` carefully: reset/check `errno`, validate end pointer, and validate target range.

## File descriptors

Every FD must have one clear owner. On multi-step constructors, unwind previously acquired resources in reverse order on failure.

Prefer CLOEXEC variants at creation time to avoid races.

Do not busy-loop on `EAGAIN`. Do not retry every `errno`; retry only documented transient cases.

## Time

For elapsed sampling intervals use `CLOCK_MONOTONIC`, not wall clock. Wall-clock adjustments must not affect CPU-rate calculations.

Handle nanosecond arithmetic with overflow-aware integer logic. Avoid floating point until presentation/calculation boundaries where it is actually useful.

## Parsing kernel pseudo-files

Kernel file formats are external interfaces. Parsers must:
- accept documented field ordering flexibility where applicable;
- ignore documented unknown future keys when safe;
- reject malformed numeric values;
- distinguish missing required fields from zero values;
- handle `max` explicitly for controller limits that support it;
- be testable entirely from string fixtures without requiring a live host.

## Logging

Keep logs sparse. A high-frequency sampling loop must not log successful samples. Log lifecycle changes and rate-limit/reduce repeated failures.

## Review checklist

- [ ] bounds are explicit
- [ ] ownership is explicit
- [ ] all FDs close on every path
- [ ] no unchecked conversion
- [ ] no shell/runtime command execution
- [ ] monotonic time for deltas
- [ ] malformed input tested
- [ ] special values tested
- [ ] sanitizer-clean
