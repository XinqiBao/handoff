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
| L1C | complete | Sequence-contract evidence | Controlled comparisons characterize single-consumer publication, reliable fan-out, and fixed dependency contracts. |
| L1D | complete | Offered-load evidence | Controlled unpaced, calibrated pacing, and bounded observer-stall runs characterize lossy delivery shares. |
| L1E | next | PMU investigation and L1 review | Apply selective hardware evidence to an observed question, consolidate durable findings, and review the post-L1 direction. |
| M1 | deferred | Bounded MPSC | Reconsider after the single-producer mechanism families establish specific multi-producer questions. |
| M2 | deferred | Multi-producer sequencing | Study selected availability or synchronization ideas only when motivated by MPSC findings. |
| M3 | deferred | SPMC work sharing and MPMC | Keep distinct from broadcast and attempt only with a concrete research question. |

The active path is
`H1 -> H2 -> C1 -> S3 -> S4 -> D1 -> D2 -> Q1 -> Q2 -> Q3 -> R1 -> R2 -> R3 -> F1 -> F2 -> W1 -> L1A -> L1B -> L1B2 -> L1C -> L1D -> L1E`.
A suitable physical Linux host and exact-revision execution clone have been verified, so controlled
measurement preparation can proceed. Multi-producer and general multi-consumer mechanisms remain
deliberately deferred; this does not defer single-producer broadcast/fan-out.

## Active stopping boundary

The portable single-producer path through W1 and controlled evidence through L1D are complete. L1E
is now locally executable. M1 through M3 remain deliberately deferred until the L1 review identifies
a concrete multi-producer research question; completing L1 does not activate them automatically.

## Next stage: L1E selective PMU investigation and milestone review

### Goal

Test one concrete explanation for the stable staged-versus-bulk throughput observation with a
targeted worker-only PMU method, then consolidate L1's durable findings and decide whether any
secondary record-layout or multi-producer question is ready to become executable.

### Evidence motivating the stage

L1B2 found that staged direct-slot access completed 4.796% more messages/s than the equal
all-or-nothing bulk route at group size 16, with all three paired blocks agreeing and 0.971% versus
0.157% sample CV. The complete routes differ in intermediate assignment, span traversal, and token
work, so fewer executed instructions per completed message is a concrete testable hypothesis.
Generic cycles and instructions are available on this host. By contrast, generic cache-miss
counters cannot test the coherence explanation for cache-line separation, variable batching
effects do not motivate a precise PMU question, and L1D's stable stall accounting does not need
hardware counters to establish its load-shape result.

### Scope

1. Reuse the canonical L1B2 workload: `bulk` versus `staged` throughput, 64-byte payload, 1024
   slots, group size 16, coordinator CPU 0, producer CPU 1, and consumer CPU 2. Hold payload
   generation and validation, completed-message count, placement, build, waiting, and host policy
   fixed.
2. Before formal collection, verify `cycles:u` and `instructions:u` availability and run one
   bounded attachment pilot. Launch a deliberately long one-trial benchmark, identify producer and
   consumer TIDs from exact CPU masks in `/proc/<pid>/task/*/status`, wait past a deliberately long
   warmup, and attach `perf stat` only to those two TIDs for a documented middle window. Reject the
   method if the TIDs, timed-phase placement, complete window, event scaling, or benchmark
   completion cannot be verified.
3. If the pilot succeeds, collect only user-mode cycles and instructions for an equal fixed middle
   window. Use long equal-count runs and three one-trial ABBA blocks, retaining six PMU windows and
   benchmark rows per implementation. Record raw counts, time enabled/running or scaling, IPC,
   counts per second, complete-trial throughput, and an explicitly approximate normalization by
   complete-trial messages/s. Do not present that approximation as exact per-message attribution.
4. Temporarily set `kernel.perf_event_paranoid` from 4 to 2 with a shell trap, restore it on every
   exit path, and verify the final value. Keep the benchmark process owned by the unprivileged user.
   Use the stock governor/EPP and L1A conditioning; retain sidecars, exact commands, TID/mask
   evidence, perf stderr, benchmark CSV, temperatures/frequencies, and throttle deltas under ignored
   `results/l1/l1e-pmu/`.
5. Decide whether the PMU evidence supports, weakens, or leaves unresolved the hypothesis that the
   staged route executes fewer instructions per completed message. Separate observed counters from
   explanations about removed assignments, compiler decisions, span traversal, or token work.
6. Add the PMU provenance and bounded interpretation to experiment 007 and the affected bulk/staged
   mechanism notes. Consolidate only conclusions already supported by L1A-L1E; do not create a
   general performance ranking.
7. Complete the L1 milestone review in the roadmap. State whether one record-layout question,
   bounded MPSC question, or neither has enough evidence and value to become the next executable
   stage. Do not activate work merely to keep the queue moving; a clean L1 boundary with no `next`
   stage is valid.

### Non-goals

- no counter sweep, model-specific event, cache-miss coherence claim, kernel profiling, or PMU
  decoration of earlier comparisons;
- no whole-process counter interpretation as timed worker work and no exact per-message claim from
  a sampled middle window;
- no source-level perf hook unless the attachment pilot demonstrably cannot delimit useful worker
  evidence and the smallest portable boundary is reviewed first;
- no rerun of scalar, sequence, offered-load, payload, capacity, batch-size, or placement matrices;
- no persistent perf, governor, kernel, boot, IRQ, or isolation change;
- no record-layout implementation campaign, general benchmark runner, MPSC/MPMC implementation, or
  automatic activation of deferred work.

### Acceptance criteria

- Every command runs from one clean, CI-green exact SHA in a fresh native Linux Release build after
  the required correctness and quality gates.
- The attachment pilot verifies exact worker TIDs and masks, starts after warmup, covers the complete
  requested middle window, and leaves benchmark checksums and affinity metadata valid. Formal
  events are available without unacceptable multiplexing or scaling.
- Three ABBA blocks retain every benchmark row and PMU window. Analysis reports medians,
  dispersion, block direction, raw/scaled counts, IPC, counts per second, throughput, and the limits
  of approximate normalization. Effects near dispersion or with inconsistent direction remain
  inconclusive.
- Perf policy is restored to 4 and verified; no persistent host state changes; temperature,
  frequency, and throttle evidence is retained; raw artifacts exist on both hosts under ignored
  `results/l1/l1e-pmu/`.
- Experiment 007 and affected mechanism notes show the complete observation -> hypothesis ->
  targeted evidence -> bounded interpretation chain with exact throughput and PMU revisions.
- Both worktrees and the complete diff are reviewed; coherent commits are pushed; required CI is
  green; ignored active status records the clean L1 boundary and any evidence-backed next stage.
- The roadmap marks L1E complete, records the milestone decision, and has either exactly one
  evidence-backed `next` stage or no `next` stage when further work remains deliberately deferred.

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
