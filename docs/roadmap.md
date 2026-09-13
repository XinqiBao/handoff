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
| R3 | next | Descriptor ring and payload storage | Separate compact descriptors from payload bytes and define their reservation, publication, and reuse contracts. |
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

## Next stage: R3

Goal: establish a bounded SPSC record mechanism whose fixed descriptor ring and variable payload
byte ring have explicit, independent capacity and reuse contracts.

Required work:

- add one concrete descriptor/payload mechanism under a mechanism-named public directory; retain R1
  and R2 as distinct baselines, reuse the logical `RecordHeader`, and avoid a storage-policy
  hierarchy, runtime registry, universal queue interface, or Firedancer compatibility surface;
- own a compile-time-sized exact-capacity descriptor array and a separate compile-time-sized
  `std::byte` payload buffer. Each descriptor stores the logical header and the physical payload
  location needed to find contiguous bytes; monotonic descriptor and byte positions define
  reservation, publication, consumption, and reuse without pointers into caller storage;
- align payload starts and footprints to 16 bytes. If a non-empty payload does not fit in the
  remaining physical suffix, account for that suffix as an internal byte reservation and place the
  payload at offset zero; do not store or publish an in-band padding header. Bound one payload so a
  valid record can make progress from any aligned empty-buffer offset. A zero-length payload consumes
  one descriptor but no payload bytes;
- provide copy-in/copy-out SPSC operations over caller-owned byte spans. A push succeeds only when
  one descriptor and the complete payload transition, including any end gap, are both available;
  otherwise it publishes neither resource. Reject inconsistent lengths or individually
  unrepresentable payloads separately from full, and reject undersized output separately from empty,
  without consuming state or partially mutating outputs;
- make the descriptor-tail release store the sole publication point after both payload bytes and
  descriptor fields are written. The consumer acquire-loads that tail before copying, then releases
  payload bytes and the descriptor slot only after the copy completes; document why independently
  observed reuse progress can only cause conservative false-full results, never premature overwrite;
- document native descriptor-slot and payload-byte capacities, alignment gaps, counter bounds,
  ownership, payload contiguity, failure stability, and the distinction from R2 padding-header
  parsing and from later sequence-addressed, broadcast, or lossy metadata mechanisms;
- test descriptor-full and byte-full states, exact capacity accounting, zero and maximum payloads,
  alignment, physical wrap gaps, failed atomic reservation, undersized output across wrap, FIFO
  header/payload integrity, repeated descriptor and byte reuse, and a long concurrent mixed-length
  run;
- add scalar throughput and ping-pong plumbing using the existing payload sizes and an explicit
  descriptor implementation name. Require both `--capacity 64|1024` and
  `--capacity-bytes 4096|65536` as its native dimensions, keep benchmark-side work and phase
  boundaries equivalent, populate both CSV capacity fields, and reject batching or option
  combinations that misstate the mechanism;
- add a question-led planned experiment comparing the descriptor/payload split with the existing
  record layouts. Treat descriptor access, payload copies, alignment, and wrap gaps as timed work and
  record no performance conclusion without controlled Linux evidence.

Non-goals: returned storage views, public reservation tokens, external payload ownership,
scatter/gather I/O, batching, descriptor-only observation, sequence-addressed lookup, broadcast,
consumer dependencies, overwrite or lossy delivery, custom allocation, serialization frameworks,
multiple producers or consumers, F1/F2 work, deferred roadmap work, performance measurements, or
performance conclusions.

Validation: mechanism-specific and shared correctness tests; benchmark CLI, CSV, and smoke
plumbing; Debug and Release tests; ASan/UBSan; practical TSan; clang-format; clang-tidy;
documentation-link checks; complete diff review; push; and required CI.

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
