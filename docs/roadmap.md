# Roadmap

This file is the canonical execution queue and completion record. It expresses technical order,
not dates. Earlier findings may reorder later stages, but changes should preserve explicit
dependencies and keep one stage marked `next`.

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
| H1 | next | Harness and contract hardening | Split the benchmark executable by direct responsibility; complete metadata, CLI validation, and current payload-contract tests. |
| S3 | queued | Cached remote indices | Preserve a distinct SPSC variant that reduces shared-index reads without batching or layout changes beyond what the mechanism needs. |
| S4 | queued | All-or-nothing SPSC batching | Add fixed-count batch operations and isolate publication granularity from other changes. |
| D1 | queued | DPDK-inspired SP/SC | Study separate head reservation and tail publication, fixed-count bulk, best-effort burst, and staged direct access in a small SP/SC mechanism. |
| Q1 | queued | Sequence publication baseline | Introduce monotonic sequence claiming, publication, producer cursor, and single-consumer gating without a full Disruptor API. |
| Q2 | queued | Disruptor-inspired fan-out | Add independent reliable consumers, slowest-reader gating, and explicit dependency semantics. |
| R1 | queued | Fixed header/payload slots | Study fixed-capacity records with explicit header and inline payload layout. |
| R2 | queued | Variable record byte ring | Store contiguous aligned `[header][payload]` records in one circular byte buffer using padding markers at wrap. |
| R3 | queued | Descriptor ring and payload storage | Separate compact descriptors from payload bytes and define their reservation, publication, and reuse contracts. |
| F1 | queued | Firedancer-inspired metadata/data handoff | Combine sequence-addressed metadata, chunk-addressed payload, consumer progress, broadcast, and detectable overwrite semantics. |
| W1 | queued | Load-shape workloads | Add burst, imbalance, temporary-stall, and offered-load latency experiments only as required by implemented mechanisms. |
| L1 | blocked-external | Controlled Linux measurements | Run pinned same-NUMA comparisons, verify effective affinity, collect host metadata, and use external `perf` where justified. |
| M1 | deferred | Bounded MPSC | Reconsider after the single-producer mechanism families establish specific multi-producer questions. |
| M2 | deferred | Multi-producer sequencing | Study selected availability or synchronization ideas only when motivated by MPSC findings. |
| M3 | deferred | SPMC work sharing and MPMC | Keep distinct from broadcast and attempt only with a concrete research question. |

The active path is `H1 -> S3 -> S4 -> D1 -> Q1 -> Q2 -> R1 -> R2 -> R3 -> F1 -> W1`.
`L1` can run when a suitable Linux host is available and does not block portable mechanism work.
Multi-producer and general multi-consumer mechanisms are deliberately deferred; this does not defer
single-producer broadcast/fan-out.

## Next stage: H1

Goal: make the existing harness and contracts strong enough to add several mechanism families
without turning the benchmark executable into a framework.

Required work:

- split `apps/handoff-bench/main.cpp` into a few direct-responsibility files for options/dispatch,
  throughput, ping-pong, and output or metadata where the existing code supports that boundary;
- retain explicit switch- or table-based dispatch; do not add registries, abstract queue bases,
  factories, a benchmark DSL, or a general configuration framework;
- add lightweight result metadata for git revision and dirty state, compiler and version, build
  mode, CPU model, requested and effective CPU placement, and affinity outcome where available;
- clarify that `capacity_bytes` describes nominal payload bytes in slots, not the full object
  footprint, renaming the field only if migration is documented;
- document and test the current default-constructed, assignment-reused payload lifetime contract;
- strengthen CLI and CSV integration tests for invalid and incompatible input;
- keep workload semantics and timed regions unchanged unless a concrete defect requires a focused
  correction.

Non-goals: a new queue mechanism, explicit-lifetime storage, a generic harness architecture,
performance conclusions, Linux topology discovery, or low-level timer changes.

Validation: Debug and Release tests, ASan/UBSan, practical TSan, clang-format, clang-tidy, benchmark
smoke runs, CSV inspection, documentation-link checks, complete diff review, push, and required CI.

## Direction after H1

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
diff contains no accidental scope, one coherent commit is pushed, and required CI is green. A
conversation may complete several such stages; stage boundaries must remain visible in history.
