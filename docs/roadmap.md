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
| R2 | next | Variable record byte ring | Store contiguous aligned `[header][payload]` records in one circular byte buffer using padding markers at wrap. |
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

## Next stage: R2

Goal: establish a bounded SPSC byte ring that packs variable-length records contiguously and makes
physical wrap explicit with padding headers.

Required work:

- add a distinct concrete byte-ring mechanism under `record`; retain R1 as the fixed-slot baseline
  and reuse its 16-byte logical sequence/type/length header contract where that keeps the two
  mechanisms comparable, without introducing a storage-policy hierarchy or queue base class;
- own one compile-time-sized `std::byte` buffer, require its size to be a multiple of 16 bytes, and
  represent producer and consumer progress as monotonic byte positions; define a normal physical
  footprint as `align_up(16 + payload_length, 16)` so every record begins at a 16-byte boundary;
- keep each normal `[header][payload]` record physically contiguous. When the remaining suffix
  cannot hold the complete next record, publish a 16-byte padding header whose `UINT32_MAX` type tag
  reserves the marker and whose length field encodes the complete skipped suffix, then place the
  record at offset zero; padding consumes capacity and is skipped by the consumer but is never
  returned as a message;
- provide explicit copy-in/copy-out SPSC operations over caller-owned byte spans. Reject a reserved
  type tag, inconsistent header length, an individually unrepresentable record, or an undersized
  output buffer without publication, consumption, or partial output mutation; distinguish these
  contract failures from ordinary full or empty states without exceptions in the hot path;
- compute admission from total occupied bytes, including alignment and any required padding, so a
  successful push reserves and publishes the whole transition atomically. A successful pop copies
  one logical header and payload before releasing its complete physical footprint;
- document the acquire/release publication and reuse edges, padding visibility, alignment bytes,
  unsigned counter bounds, ownership, and the fact that neither records nor headers may straddle the
  physical end;
- test empty/full behavior, exact byte accounting, zero and maximum payloads, mixed lengths, aligned
  footprints, padding insertion and skipping, repeated wrap, FIFO/header/payload integrity, every
  failed-operation stability rule, slot-byte reuse, and a long concurrent variable-length run;
- add scalar throughput and ping-pong plumbing using the existing payload sizes and a dedicated
  `--capacity-bytes 4096|65536` option. Keep phase boundaries, waiting, validation, and trial
  accounting equivalent; populate `capacity_bytes`, leave `capacity_slots` empty, and reject
  slot-capacity or batch combinations that would misstate the mechanism;
- add a question-led planned experiment that treats alignment, padding, header parsing, and byte
  copies as part of the byte-ring contract and records no performance conclusion without controlled
  Linux evidence.

Non-goals: split records, implicit wrap without a marker, descriptor/payload separation, external
payload ownership, scatter/gather I/O, returned direct-storage views, reservation tokens, batching,
overwrite or lossy behavior, custom allocation, serialization frameworks, multiple producers or
consumers, deferred roadmap work, performance measurements, or performance conclusions.

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
