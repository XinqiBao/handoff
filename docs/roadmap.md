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
| Q3 | next | Disruptor-inspired dependencies | Add consumer dependency gating as a separate sequencing experiment. |
| R1 | queued | Fixed header/payload slots | Study fixed-capacity records with explicit header and inline payload layout. |
| R2 | queued | Variable record byte ring | Store contiguous aligned `[header][payload]` records in one circular byte buffer using padding markers at wrap. |
| R3 | queued | Descriptor ring and payload storage | Separate compact descriptors from payload bytes and define their reservation, publication, and reuse contracts. |
| F1 | queued | Firedancer-inspired metadata ring | Study sequence-addressed metadata, independent consumer progress, broadcast observation, and detectable overwrite. |
| F2 | queued | Firedancer-inspired metadata/data handoff | Combine the metadata mechanism with chunk-addressed payload storage and explicit reuse/publication rules. |
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

## Next stage: Q3

Goal: isolate consumer dependency gating by adding a fixed two-stage reliable sequence pipeline in
which the downstream consumer may observe a sequence only after the upstream consumer releases it.

Required work:

- add a distinct one-producer, two-consumer dependency mechanism; keep Q1 and Q2 unchanged and
  preserve one-based finite sequence semantics, exact capacity, default-constructed slots, and
  explicit claim/populate/publish behavior;
- give the upstream and downstream consumers stable compile-time roles, owner-local
  next-to-observe state, atomic progress sequences, and at most one move-only observation token per
  role; reject nested observations without dynamic registration or runtime dependency graphs;
- gate upstream observation on producer publication, downstream observation on upstream release,
  and producer reuse on downstream release, so the dependency is part of availability rather than
  a benchmark-side convention;
- retain producer claim cancellation and per-consumer observation cancellation semantics; require
  the ring to outlive all tokens and confine the producer and each consumer role to its declared
  owner thread;
- write the C++ acquire/release argument across producer publication, upstream observation and
  release, downstream observation and release, and producer reuse; do not add a general sequence
  barrier, arbitrary DAG, multiple producers, per-slot availability, or alternate waiting
  strategies;
- cover initial state, role ordering, unavailable downstream observations, dependency release and
  cancellation, exact capacity, slow downstream backpressure, slot wrap, move-only tokens,
  finite-limit rejection, resource retention, and long concurrent integrity with independently
  paced upstream and downstream consumers;
- add a dependency throughput workload whose completion count means every publication passed both
  stages and was released downstream, with equivalent validation/checksum work at both stages; do
  not force the pipeline into scalar ping-pong;
- add a question-led planned experiment with LMAX Disruptor provenance and no performance conclusion
  without controlled Linux evidence.

Non-goals: LMAX API or Java compatibility, arbitrary consumer dependency graphs, sequence barrier
APIs, handler DSLs, multiple producers, work sharing, lossy overwrite, dynamic consumer
registration or removal, per-slot publication tracking, batch claims, alternate waiting strategies,
performance measurements, or performance conclusions.

Validation: mechanism-specific and shared correctness tests; benchmark CLI, CSV, and smoke
plumbing; Debug and Release tests; ASan/UBSan; practical TSan; clang-format; clang-tidy;
documentation-link checks; complete diff review; push; and required CI.

## Direction after Q2

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
