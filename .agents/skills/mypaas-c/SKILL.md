# Skill: mypaas-c

Use this skill for C implementation and review in `mypaas-statd`.

## Objective

Produce boring, explicit, memory-safe-by-discipline C17 suitable for a small privileged Linux daemon. Prefer clarity, bounded behavior, and mature interfaces over clever abstractions.

The target is not "advanced C". The target is small C code that is easy to reason about, test, profile, and maintain.

## Simplicity rule

Implement the smallest correct design that satisfies the active phase in `docs/PHASES.md`.

Prefer:
- straightforward functions;
- plain structs;
- explicit ownership;
- direct error propagation;
- standard C/POSIX/Linux APIs;
- local, obvious state;
- fixed/documented limits where natural;
- small duplicated code over a premature generic framework when the abstraction would hide semantics.

Do not introduce an abstraction only because two short call sites look similar. Refactor when a real stable pattern exists and the resulting API makes ownership, bounds, and error behavior clearer.

Avoid over-engineered patterns such as:
- home-grown object systems;
- generic collection libraries for a few bounded records;
- macro-heavy generic programming;
- opaque callback chains;
- custom allocators;
- thread pools;
- elaborate dependency injection;
- extensibility/plugin layers for hypothetical features.

A mature implementation should have fewer moving parts, not merely fewer lines.

## Required workflow

1. Read `AGENTS.md`, `docs/PHASES.md`, and the relevant contract under `docs/`.
2. Identify the active phase and do not pull later-phase machinery into the task.
3. Identify ownership/lifetime for every object touched.
4. Identify all file descriptors acquired/released.
5. Define input bounds before parsing or allocation.
6. Implement the smallest change that satisfies the contract.
7. Add tests/fixtures before optimizing.
8. Run compiler warnings and sanitizers.
9. Simplify the implementation if complexity remains without a demonstrated requirement.

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

Do not keep descriptors open solely because persistent FDs sound faster. Start with the simpler lifecycle and retain persistent descriptors only when correctness remains clear and measurement shows a useful benefit.

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

Prefer one small parser per format or closely related format family. Do not build a generic parser framework unless the actual formats demonstrate a stable shared grammar that makes the result simpler.

## Logging

Keep logs sparse. A high-frequency sampling loop must not log successful samples. Log lifecycle changes and rate-limit/reduce repeated failures.

## Review checklist

- [ ] active phase respected
- [ ] implementation is the smallest correct design
- [ ] bounds are explicit
- [ ] ownership is explicit
- [ ] all FDs close on every path
- [ ] no unchecked conversion
- [ ] no shell/runtime command execution
- [ ] monotonic time for deltas
- [ ] malformed input tested
- [ ] special values tested
- [ ] no speculative abstractions
- [ ] sanitizer-clean
