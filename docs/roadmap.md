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
| L1B2 | next | Grouped SPSC evolution evidence | Measure publication granularity and staged direct-slot access after the scalar comparisons are understood. |
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

The portable single-producer path through W1, Linux measurement integrity, and scalar SPSC evidence
are complete. L1B2 is now locally executable. L1C through L1E are ordered measurement outcomes
whose detailed contracts must be written only when they become next. M1 through M3 remain
deliberately deferred until a concrete multi-producer research question justifies reopening that
path after the L1 review.

## Next stage: L1B2 grouped SPSC evolution evidence

### Goal

Measure two bounded grouped-operation questions on the verified Linux host: how fixed all-or-nothing
publication groups compare with scalar publication, and how direct staged ring-slot access compares
with an equal all-or-nothing bulk transfer through intermediate arrays.

### Evidence motivating the stage

L1B found a stable conditional benefit for the complete cache-line-separated scalar variant, while
cached-index throughput was directionally positive but variable and cached-index ping-pong was
inconclusive. Neither result calls for a sensitivity matrix or immediate PMU work. The next distinct
question is publication granularity under saturated throughput. Existing batch, bulk, and staged
mechanisms have passed their correctness gates and share benchmark-side group generation and
validation designed for these comparisons.

### Scope

1. Compare `basic` with `batch` at batch sizes 1, 4, and 16 using throughput only. This changes the
   complete operation shape from scalar publication to all-or-nothing group publication while the
   workload still generates and validates equal fixed groups on both paths.
2. Compare `bulk` with `staged` at group size 16 using throughput only. This holds all-or-nothing
   group progress fixed while changing from transfers through producer/consumer arrays to direct
   writable/readable ring spans with explicit finish operations.
3. Use only 64-byte payloads and 1024 exact usable slots. Add group size 4 to `bulk` versus `staged`
   only if the size-16 result is ambiguous and that specific sensitivity can resolve it. Do not add
   payload or capacity matrices automatically.
4. Pilot a common completed-message count so the fastest timed trial in each pair is at least
   roughly two seconds. Hold total completed messages, warmup, placement, build, payload work,
   group size, and command order fixed within each comparison.
5. Use one-trial ABBA blocks and retain at least six rows per implementation/configuration. Prefer
   three blocks initially; add a fourth only when the first three reveal bounded order behavior
   that the extra block can distinguish. Preserve every row without outlier removal.
6. Use the L1A warm-state conditioning method, stock HWP policy, coordinator CPU 0, producer CPU 1,
   and consumer CPU 2. Retain the exact commands, host sidecar, verified placement metadata,
   temperature/frequency observations, and throttle-counter deltas beside raw CSV.
7. Update experiment records 005 and 007 with exact provenance, summaries, interpretations, and
   limits. Keep experiment 006 planned: the current steady-state workload does not deliberately
   constrain availability or count partial burst calls, so it cannot answer the bulk-versus-burst
   partial-progress question.

### Non-goals

- no ping-pong, sequence, fan-out, pipeline, record-layout, or offered-load comparison;
- no `bulk` versus `burst` measurement or claim about partial-progress behavior;
- no automatic payload, capacity, or group-size matrix;
- no end-to-end no-copy, DPDK compatibility, or isolated-instruction claim;
- no PMU collection unless a stable grouped-operation result creates a concrete cycles or
  instructions hypothesis that cannot be answered from the timed results alone;
- no benchmark framework, persistent host tuning, MPSC, SPMC, or MPMC work.

### Acceptance criteria

- Every command runs from one clean, CI-green exact SHA in a fresh native Linux Release build after
  the required correctness and quality gates.
- All requested worker affinities are reported as applied with matching effective CPU masks;
  checksums agree within each configuration; total completed messages are equal; no retained row is
  silently discarded.
- The fastest retained trial in each configuration is at least roughly two seconds, with at least
  six retained rows per implementation and interleaved ABBA order.
- Analysis reports medians, sample dispersion, full range, block behavior, and pairwise ratios. An
  effect near the observed host noise or with inconsistent paired direction is reported as
  inconclusive rather than ranked.
- Experiment records 005 and 007 contain exact provenance, controlled and changed variables,
  results, bounded interpretations, limitations, and reproduction commands. Raw evidence remains
  under ignored `results/l1/l1b2-grouped/` on Linux and Mac.
- Both worktrees and the complete diff are reviewed; coherent commits are pushed; required CI is
  green; ignored active status records the evidence location and exact next stage.
- On completion, replace this contract with one detailed L1C contract revised from the accumulated
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
