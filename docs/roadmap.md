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
| L1B | next | Scalar SPSC evolution evidence | Run bounded canonical comparisons for counter placement and remote-index caching. |
| L1B2 | queued | Grouped SPSC evolution evidence | Measure publication granularity and staged direct-slot access after the scalar comparisons are understood. |
| L1C | queued | Sequence-contract evidence | Measure selected single-consumer publication, reliable fan-out, and fixed dependency costs with explicit placement and semantic limits. |
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

The portable single-producer path through W1 and the Linux measurement-integrity stage are complete.
L1B is now locally executable. L1B2 through L1E are ordered measurement outcomes whose detailed
contracts must be written only when they become next. M1 through M3 remain deliberately deferred
until a concrete multi-producer research question justifies reopening that path after the L1 review.

## Next stage: L1B scalar SPSC evolution evidence

### Goal

Establish the first controlled mechanism evidence on the verified Linux host. Measure, pairwise,
what changes when the basic ring separates its producer/consumer counters and when it caches remote
progress, under one canonical throughput and one canonical ping-pong workload.

### Evidence motivating the stage

L1A removed coordinator interference, verified effective Linux affinity, and added fixed
multi-consumer placement. At its CI-green measurement revision, all Linux correctness, format,
clang-tidy, ASan/UBSan, and TSan gates passed. A stock-HWP warm-state baseline using CPU 0 for the
blocked coordinator and CPUs 1/2 for workers retained seven trials longer than 2.4 seconds with
0.280% sample CV, 0.915% full range, no material order drift, and no thermal-throttle events. This
supports bounded relative comparisons without changing governor or EPP.

### Scope

1. Compare `basic` versus `cache-line` to characterize the complete structural effect of separating
   the producer-owned tail and consumer-owned head state blocks.
2. Separately compare `basic` versus `cached-index` to characterize the complete effect of retaining
   local remote-progress bounds and refreshing them only when a cached bound blocks progress.
3. Use only 64-byte payloads and 1024 exact usable slots initially. Run both throughput and
   ping-pong because remote-progress caching has different opportunity in saturated streaming and
   dependency-bound exchange.
4. Pilot one iteration count per workload so the fastest timed trial is at least roughly two
   seconds. Hold that count, warmup, placement, build, and benchmark-side work fixed within every
   pairwise comparison.
5. For each pair and workload, use four three-trial commands in ABBA order. This retains six rows
   per implementation, limits simple command-order drift, and preserves every trial without
   outlier removal.
6. Use the L1A warm-state conditioning method, stock HWP policy, coordinator CPU 0, producer CPU 1,
   and consumer CPU 2. Retain a host sidecar, verified placement metadata, temperature/frequency
   observations, and throttle-counter deltas beside raw CSV.
7. Update experiment records 003 and 004 with the exact measurement SHA, commands, per-trial
   summaries, pairwise results, interpretation, and limits. Promote only conclusions that survive
   review; keep causal coherence claims out unless later targeted PMU evidence supports them.

### Non-goals

- no batch, bulk, burst, staged, sequence, fan-out, pipeline, record-layout, or offered-load
  comparison;
- no automatic payload/capacity sensitivity matrix unless the canonical result is genuinely
  ambiguous and one specific sensitivity can resolve it;
- no claim that object layout isolates a single coherence event or that cached indices isolate only
  an atomic instruction;
- no PMU collection before an observed result creates a specific hypothesis;
- no benchmark framework, persistent host tuning, MPSC, SPMC, or MPMC work.

### Acceptance criteria

- Every command runs from one clean, CI-green exact SHA in a fresh native Linux Release build after
  the required correctness and quality gates.
- All requested worker affinities are reported as applied with matching effective CPU masks;
  checksums agree within each workload/configuration; no retained row is silently discarded.
- The fastest retained trial in each workload is at least roughly two seconds, with six retained
  rows per implementation per pair and ABBA command order.
- Analysis reports medians, sample dispersion, full range, order behavior, and pairwise ratios. An
  effect near the observed host noise is reported as inconclusive rather than ranked.
- Experiment records 003 and 004 contain exact provenance, controlled and changed variables,
  results, bounded interpretations, limitations, and reproduction commands. Raw evidence remains
  under ignored `results/l1/l1b-scalar/` on Linux and Mac.
- Both worktrees and the complete diff are reviewed; coherent commits are pushed; required CI is
  green; ignored active status records the evidence location and exact next stage.
- On completion, replace this contract with one detailed L1B2 contract, revised from the scalar
  evidence rather than copied mechanically from the queued outcome.

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
