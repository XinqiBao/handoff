# Roadmap

This file is the canonical execution queue and completion record. It expresses technical order,
not dates. Earlier findings may reorder later stages, but changes should preserve explicit
dependencies and keep one stage marked `next` while locally executable work remains.

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
| F2 | complete | Firedancer-inspired metadata/data handoff | Combine the metadata mechanism with chunk-addressed payload storage and explicit reuse/publication rules. |
| W1 | complete | Load-shape workloads | Add one F2 offered-load workload covering unpaced pressure, producer pacing, and temporary observer stalls. |
| L1A | complete | Linux measurement integrity | Remove observed harness interference, verify effective placement, support controlled fixed multi-consumer placement, and establish a reproducible stock-host baseline. |
| L1B | complete | Scalar SPSC evolution evidence | Controlled canonical comparisons characterize counter placement and remote-index caching. |
| L1B2 | complete | Grouped SPSC evolution evidence | Controlled comparisons characterize publication groups and staged direct-slot access. |
| L1C | next | Sequence-contract evidence | Measure selected single-consumer publication, reliable fan-out, and fixed dependency costs with explicit placement and semantic limits. |
| L1D | queued | Offered-load evidence | Characterize unpaced pressure, calibrated producer pacing, and bounded observer stalls without a Cartesian sweep. |
| L1E | queued | PMU investigation and L1 review | Apply selective hardware evidence to observed questions, consolidate durable findings, and review the post-L1 direction. |
| M1 | deferred | Bounded MPSC | Reconsider after the single-producer mechanism families establish specific multi-producer questions. |
| M2 | deferred | Multi-producer sequencing | Study selected availability or synchronization ideas only when motivated by MPSC findings. |
| M3 | deferred | SPMC work sharing and MPMC | Keep distinct from broadcast and attempt only with a concrete research question. |

The active path is
`H1 -> H2 -> C1 -> S3 -> S4 -> D1 -> D2 -> Q1 -> Q2 -> Q3 -> R1 -> R2 -> R3 -> F1 -> F2 -> W1 -> L1A -> L1B -> L1B2 -> L1C -> L1D -> L1E`.
A suitable physical Linux host and exact-revision execution clone have been verified, so controlled
measurement preparation can proceed. Multi-producer and general multi-consumer mechanisms remain
deliberately deferred; this does not defer single-producer broadcast/fan-out.

## Active stopping boundary

The portable single-producer path through W1, Linux measurement integrity, and scalar and grouped
SPSC evidence are complete. L1C is now locally executable. L1D and L1E are ordered measurement
outcomes whose detailed contracts must be written only when they become next. M1 through M3 remain
deliberately deferred until a concrete multi-producer research question justifies reopening that
path after the L1 review.

## Next stage: L1C sequence-contract evidence

### Goal

Measure three bounded sequence-contract questions on the verified Linux host: how the complete
single-consumer sequence implementation compares with basic head/tail SPSC, what completed-
publication cost accompanies reliable delivery to two independent consumers, and how a fixed
upstream/downstream dependency compares with independent fan-out when both perform two validations.

### Evidence motivating the stage

L1B2 found no stable size-1 batch ranking, a directionally repeated but variable size-4 batch
advantage, a small positive size-16 batch observation, and a clear conditional size-16 staged
advantage over bulk. Those grouped results do not require another sensitivity or immediate PMU
work. The next distinct questions concern richer sequencing contracts. The sequence, fan-out, and
pipeline mechanisms have passed their correctness gates, and L1A added verified placement and
blocked coordinator control for both one- and two-consumer workloads.

### Scope

1. Compare `basic` with `sequence` in throughput and ping-pong at a 64-byte payload and 1024 exact
   usable slots. Treat this as an implementation comparison: head/tail assignment and the complete
   claim/populate/publish/observe/release design differ in token lifecycle, local state, direct
   throughput slot access, and consumer value-transfer shape.
2. Compare `sequence` with `fan-out` in throughput at the same payload and capacity. Count completed
   publications, not aggregate deliveries, and state explicitly that `fan-out` performs two
   reliable deliveries and uses an additional worker. This comparison characterizes the richer
   contract; it does not isolate consumer count, an atomic operation, or topology.
3. Compare `fan-out` with `pipeline` in throughput at the same payload and capacity. Both routes use
   one producer, two consumers, two validations, and downstream-complete publication counts. The
   changed contract is independent observation with minimum gating versus fixed ordered dependency
   and downstream gating.
4. Pilot iteration and warmup counts separately for scalar throughput, ping-pong, and three-worker
   throughput so the fastest timed row in every pair is at least roughly two seconds. Hold counts,
   build, payload work, placement, result semantics, and command order fixed within each pair.
5. Use one-trial ABBA blocks and retain at least six rows per implementation/configuration. Start
   with three blocks; add a fourth only when bounded order behavior or dispersion makes it capable
   of resolving the stated comparison. Preserve every row without outlier removal.
6. Use the L1A warm-state conditioning method and stock HWP policy. Restrict each process to
   coordinator CPU 0. Place the producer on CPU 1 and the single consumer on CPU 2; place fan-out
   consumer 0 or pipeline upstream on CPU 2 and fan-out consumer 1 or pipeline downstream on CPU 3.
   Retain verified effective masks for every role. Record CPU 3's greater historical network
   softirq activity as a four-core-host limitation.
7. Retain exact commands, a host sidecar, temperature/frequency observations, and throttle-counter
   deltas beside raw CSV under ignored `results/l1/l1c-sequence/`. Update experiment records 008,
   009, and 010 with exact provenance, all retained summaries, interpretations, and semantic limits.
   Promote only stable observations to the three sequence mechanism notes.

### Non-goals

- no payload, capacity, placement, consumer-count, slow-consumer, or waiting-strategy matrix;
- no offered-load, record-layout, grouped-operation, or bulk/burst measurement;
- no isolated cost claim for sequence numbering, one atomic, one token, or one delivery;
- no claim of LMAX API, ABI, Java-memory-model, or benchmark compatibility;
- no equation of one-consumer and two-consumer semantic work, and no general dependency graph;
- no PMU collection unless a stable result creates a concrete hypothesis that timed evidence cannot
  answer; L1E remains the intended selective hardware-evidence stage;
- no source or benchmark-framework expansion, persistent host tuning, MPSC, SPMC, or MPMC work.

### Acceptance criteria

- Every command runs from one clean, CI-green exact SHA in a fresh native Linux Release build after
  the required correctness and quality gates.
- Single- and two-consumer affinity requests report applied placement with exact verified masks;
  checksums agree for every required consumer or stage; completed-publication counts and workload
  semantics match inside each comparison; no retained row is silently discarded.
- The fastest retained trial in every pair is at least roughly two seconds, with at least six rows
  per implementation/configuration and interleaved one-trial ABBA order.
- Analysis reports medians, sample dispersion, full range, block behavior, and pairwise ratios. An
  effect near host noise or with inconsistent paired direction is inconclusive rather than ranked.
- Records 008-010 distinguish implementation comparison from richer-contract comparison, identify
  one versus two deliveries and two independent versus ordered validations, and contain exact
  provenance, controlled and changed variables, results, limits, and reproduction commands. Raw
  evidence remains under ignored `results/l1/l1c-sequence/` on Linux and Mac.
- Both worktrees and the complete diff are reviewed; coherent commits are pushed; required CI is
  green; ignored active status records the evidence location and exact next stage.
- On completion, replace this contract with one detailed L1D contract revised from the accumulated
  L1 evidence rather than copied mechanically from the queued outcome.

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
