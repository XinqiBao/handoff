# Roadmap

This file is the canonical execution queue and completion record. It expresses technical order,
not dates. Earlier findings may reorder later stages, but changes should preserve explicit
dependencies and keep one stage marked `next`.

Only the unique `next` stage receives a full execution contract below. Queued stages retain a
one-line outcome and only the acceptance constraints needed to preserve an existing decision. When
the next stage completes, replace its contract with one for its successor instead of retaining a
growing archive of stage plans. Mechanism and experiment history belongs in their respective
documents and Git.

## Status meanings

- `complete`: implemented, validated, documented, committed, and pushed;
- `next`: the next locally executable stage;
- `queued`: ordered future work whose prerequisites are not yet complete;
- `blocked-external`: ready in principle but requires an unavailable environment or evidence;
- `deferred`: intentionally outside the active path and reconsidered only after higher-value work.

## Execution queue

| ID | Status | Stage | Outcome |
| --- | --- | --- | --- |
| B0 | complete | Repository bootstrap | Portable C++23 build, tests, tooling, CI, platform skeleton, and project documentation. |
| S1 | complete | Basic bounded SPSC | Fixed inline slots, exact usable capacity, non-blocking FIFO operations, and conservative publication ordering. |
| B1 | complete | Baseline SPSC workloads | Shared throughput and ping-pong workloads, trials, summaries, CSV output, and optional affinity. |
| S2 | complete | Cache-line-separated SPSC | A preserved variant changing only producer/consumer counter placement. |
| H1 | complete | Benchmark source decomposition | Split the benchmark executable by direct responsibility without changing workload or result behavior. |
| H2 | complete | CLI, result, and metadata hardening | Correct known failure paths, make result semantics explicit, and record reliable run metadata. |
| C1 | complete | Existing SPSC contract coverage | Complete payload/lifetime tests and documentation for the two implemented rings. |
| S3 | complete | Cached remote indices | Preserve a distinct SPSC variant that reduces shared-index reads without batching or layout changes beyond what the mechanism needs. |
| S4 | complete | All-or-nothing SPSC batching | Add fixed-count batch operations and isolate publication granularity from other changes. |
| D1 | complete | DPDK-inspired bulk and burst | Contrast fixed-count all-or-nothing bulk operations with explicitly best-effort burst operations in SP/SC. |
| D2 | complete | DPDK-inspired staged SP/SC | Study separate head reservation and tail publication through a small `reserve -> write -> finish` API, including wrap spans. |
| Q1 | complete | Sequence publication baseline | Introduce monotonic sequence claiming, publication, producer cursor, and single-consumer gating without a full Disruptor API. |
| Q2 | complete | Disruptor-inspired fan-out | Add independent reliable consumers and explicit slowest-reader gating. |
| Q3 | complete | Disruptor-inspired dependencies | Add consumer dependency gating as a separate sequencing experiment. |
| R1 | complete | Fixed header/payload slots | Study fixed-capacity records with explicit header and inline payload layout. |
| R2 | complete | Variable record byte ring | Store contiguous aligned `[header][payload]` records in one circular byte buffer using padding markers at wrap. |
| R3 | complete | Descriptor ring and payload storage | Separate compact descriptors from payload bytes and define their reservation, publication, and reuse contracts. |
| F1 | complete | Firedancer-inspired metadata ring | Study sequence-addressed metadata, independent consumer progress, broadcast observation, and detectable overwrite. |
| F2 | next | Firedancer-inspired metadata/data handoff | Combine the metadata mechanism with chunk-addressed payload storage and explicit reuse/publication rules. |
| W1 | queued | Load-shape workloads | Add burst, imbalance, temporary-stall, and offered-load latency experiments only as required by implemented mechanisms. |
| L1 | blocked-external | Controlled Linux measurements | Run pinned same-NUMA comparisons, verify effective affinity, collect host metadata, and use external `perf` where justified. |
| M1 | deferred | Bounded MPSC | Reconsider after the single-producer mechanism families establish specific multi-producer questions. |
| M2 | deferred | Multi-producer sequencing | Study selected availability or synchronization ideas only when motivated by MPSC findings. |
| M3 | deferred | SPMC work sharing and MPMC | Keep distinct from broadcast and attempt only with a concrete research question. |

The active path is
`H1 -> H2 -> C1 -> S3 -> S4 -> D1 -> D2 -> Q1 -> Q2 -> Q3 -> R1 -> R2 -> R3 -> F1 -> F2 -> W1`.
`L1` can run when a suitable Linux host is available and does not block portable mechanism work.
Multi-producer and general multi-consumer mechanisms are deliberately deferred; this does not defer
single-producer broadcast/fan-out.

## Next stage: F2

Goal: combine sequence-addressed publication metadata with owned, chunk-addressed payload storage
while preserving caller-owned observation positions and detectable overwrite.

Required work:

- add one concrete public mechanism that owns a power-of-two array of publication slots and one
  fixed-size payload chunk per slot. Keep the chunk location explicit in metadata even though this
  first layout maps it deterministically, so metadata/data coordination is visible without adding a
  general allocator or variable reservation policy;
- accept variable logical payload lengths up to the compile-time chunk size, including zero. Copy
  bytes into ring-owned storage and copy them out to caller-owned storage; state exact chunk count,
  byte capacity, unused-byte behavior, and why this is direct chunk addressing rather than a byte
  ring or a `zero-copy` interface;
- retain F1's one-based finite sequences, direct slot lookup, single-producer ownership, independent
  caller-owned consumer positions, overwrite delivery, progress snapshot, explicit in-progress
  state, and success/not-yet/overwritten/retry classification. Add explicit invalid-input and
  output-too-small results without modifying caller output on failure;
- coordinate reuse so the producer marks the mapped publication in progress before changing its
  payload chunk. Represent payload bytes atomically, or provide an equally complete portable C++
  argument that a consumer racing reuse cannot perform a data race. A read must validate the
  publication sequence around both metadata and payload copying and never return a torn successful
  record;
- keep publication metadata as the visibility and validation authority: payload bytes and metadata
  fields are complete before the final sequence, and latest-published progress follows that final
  sequence. Document the complete conservative ordering argument and finite exhaustion behavior;
- test zero, maximum, and oversized payloads; future reads; direct chunk mapping; exact wrap
  overwrite; undersized output; failed input/output stability; independent observers; range-based
  resynchronization; finite sequence exhaustion; and a long concurrent producer/observer race with
  mixed lengths and integrity checks;
- do not adapt the mechanism to a lossless workload by adding hidden gating or retries that change
  its delivery semantics. Integrate it with benchmark code only if the workload reports offered,
  observed, and overwritten publications explicitly; otherwise record the missing workload shape
  and leave integration to W1.

Non-goals: dynamic payload allocation, variable-sized chunk reservation, records spanning chunks,
external payload ownership, backpressure, slowest-reader gating, reliable delivery, internal
consumer registration, work sharing, blocking waits, batching, shared-memory lifecycle, Firedancer
compatibility, deferred roadmap work, performance measurements, or performance conclusions.

Validation: mechanism-specific and existing regression tests; Debug and Release tests; ASan/UBSan;
practical TSan; clang-format; clang-tidy; documentation-link checks; complete diff review; push; and
required CI.

## Direction after Q3

Each mechanism stage follows the same sequence:

1. State semantics, invariants, ownership, memory-order argument, and non-goals in a mechanism note.
2. Implement the smallest locally understandable mechanism and preserve meaningful baselines.
3. Pass common and mechanism-specific correctness gates from the testing strategy.
4. Integrate only the benchmark dimensions needed for the stage and smoke-test the plumbing.
5. Create a question-led planned experiment; do not claim performance without controlled evidence.
6. Review, update this queue, commit, push, wait for CI, and continue when the next stage is eligible.

Existing SPSC acquire/release ordering is already the conservative correct baseline. Do not invent a
weaker `memory-order refinement` variant merely to fill a roadmap item; change ordering only as part
of a specific mechanism with a written C++ happens-before argument.

DPDK, LMAX Disruptor, and Firedancer remain high-priority inspirations, not ports or compatibility
targets. Their stages should reproduce named structural ideas while excluding surrounding APIs,
runtimes, allocators, networking, and platform infrastructure.

## Completion criteria

A stage is complete only when its documented semantics and implementation agree, required
correctness and quality checks pass, benchmark claims match the measurement method, the complete
diff contains no accidental scope, its coherent commit history is pushed, and required CI is green.
A conversation may complete several such stages; stage boundaries must remain visible in history.
