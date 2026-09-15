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
| L1D | next | Offered-load evidence | Characterize unpaced pressure, calibrated producer pacing, and bounded observer stalls without a Cartesian sweep. |
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

The portable single-producer path through W1 and controlled evidence through L1C are complete. L1D
is now locally executable. L1E remains an ordered measurement outcome whose detailed contract must
be written only when it becomes next. M1 through M3 remain deliberately deferred until a concrete
multi-producer research question justifies reopening that path after the L1 review.

## Next stage: L1D offered-load evidence

### Goal

Characterize how unpaced publication, one calibrated paced no-stall setting, and one or two bounded
observer-stall durations change the observed and overwritten shares of the lossy
`sequence-payload` ring on the verified Linux host.

### Evidence motivating the stage

W1 already defines and tests exact offered/observed/overwritten accounting, absolute-deadline
producer pacing, final drain, retry accounting, and periodic observer stalls. L1C completed the
lossless sequence-contract questions without creating an immediate PMU need. The remaining primary
L1 workload question is how the existing lossy sequence-payload contract behaves under bounded,
measured load shapes. Nominal sleep intervals are not arrival-rate guarantees, so a small pilot
must locate useful regimes before formal settings are fixed.

### Scope

1. Use only `sequence-payload` with a 64-byte payload, 1024 slots, coordinator CPU 0, producer CPU
   1, and observer CPU 2. Keep payload generation, observation, retry policy, warmup shape, timed
   window, final drain, build, host policy, and placement fixed except for the stated load-shape
   variable.
2. Run a bounded no-stall calibration pilot at nominal producer intervals 0, 1,000, 10,000, and
   100,000 ns. Pilot counts may differ so each setting completes promptly while still exposing a
   useful actual offered rate. Use measured offered and observed rates, not nominal intervals, to
   choose one paced no-stall setting that materially differs from unpaced pressure.
3. Starting from that one paced setting, hold the stall interval fixed at a simple documented
   observation count such as 1024. Use the pilot's actual offered rate and the ring capacity to
   choose one or two stall durations predicted to create visible but interpretable overwrite
   episodes. Change only stall duration across those settings; do not sweep pacing, stall interval,
   and duration together.
4. Pilot formal iteration and warmup counts per selected setting so each retained timed observation
   window is at least roughly two seconds without making the slowest paced group needlessly long.
   Retain at least six one-trial rows per formal setting, rotate setting order between blocks, and
   preserve every row without outlier removal.
5. Check every pilot and formal row for
   `offered_messages = observed_messages + overwritten_messages`. Retain observed and overwritten
   counts and shares, retry attempts, observed payload bytes, checksum, common elapsed window, and
   actual offered and observed rates. Report distributions and block/order behavior rather than a
   combined score.
6. Use the L1A warm-state conditioning method and stock HWP policy. Retain verified producer and
   observer masks, exact commands, a host sidecar, temperature/frequency observations, and thermal-
   throttle deltas under ignored `results/l1/l1d-offered-load/` on Linux and Mac.
7. Update experiment 014 with exact provenance, calibrated nominal and actual rates, every formal
   setting and summary, observed scheduler limitations, interpretation, and claim limits. Promote
   only stable aggregate behavior to the sequence-payload mechanism note.

### Non-goals

- no comparison with lossless queues or claim that offered-load rate is completed handoff
  throughput;
- no payload, capacity, placement, observer-count, pacing, or stall Cartesian sweep;
- no per-publication or one-way latency, arbitrary arrival process, exact scheduler pacing claim,
  or universal overload score;
- no change to overwrite/resynchronization semantics, hidden producer gating, or benchmark
  framework unless a correctness defect is demonstrated;
- no record-layout campaign, general runner, persistent host tuning, or MPSC/MPMC work;
- no PMU collection merely to decorate load-shape rows; L1E remains the selective hardware-
  evidence stage.

### Acceptance criteria

- Every command runs from one clean, CI-green exact SHA in a fresh native Linux Release build after
  the required correctness and quality gates.
- Every requested affinity is applied with an exact verified mask, every retained row satisfies the
  accounting invariant, and observed byte counts and checksums agree with the messages actually
  observed.
- The pilot remains logarithmic and bounded. The formal group contains unpaced pressure, exactly one
  paced no-stall baseline, and no more than two stall durations at that same pacing and stall
  interval, with at least six retained rows per setting and roughly two-second or longer windows.
- Analysis reports medians, sample dispersion, full range, per-setting counts and shares, actual
  rates, retry behavior, and order behavior. It distinguishes scheduler-driven pacing limitations
  from mechanism observations and makes no per-message latency inference.
- Experiment 014 contains exact provenance, controlled and changed variables, calibrated settings,
  results, limits, and reproduction commands. Raw evidence remains under ignored
  `results/l1/l1d-offered-load/` on Linux and Mac.
- Both worktrees and the complete diff are reviewed; coherent commits are pushed; required CI is
  green; ignored active status records the evidence location and exact next stage.
- On completion, replace this contract with one detailed L1E contract based on the strongest
  observation-driven PMU hypothesis from L1B through L1D and the remaining milestone-review needs.

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
