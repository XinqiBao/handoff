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
| L1A | next | Linux measurement integrity | Remove observed harness interference, verify effective placement, support controlled fixed multi-consumer placement, and establish a reproducible stock-host baseline. |
| L1B | queued | SPSC evolution evidence | Run bounded canonical comparisons for counter placement, remote-index caching, publication granularity, and staged direct-slot access. |
| L1C | queued | Sequence-contract evidence | Measure selected single-consumer publication, reliable fan-out, and fixed dependency costs with explicit placement and semantic limits. |
| L1D | queued | Offered-load evidence | Characterize unpaced pressure, calibrated producer pacing, and bounded observer stalls without a Cartesian sweep. |
| L1E | queued | PMU investigation and L1 review | Apply selective hardware evidence to observed questions, consolidate durable findings, and review the post-L1 direction. |
| M1 | deferred | Bounded MPSC | Reconsider after the single-producer mechanism families establish specific multi-producer questions. |
| M2 | deferred | Multi-producer sequencing | Study selected availability or synchronization ideas only when motivated by MPSC findings. |
| M3 | deferred | SPMC work sharing and MPMC | Keep distinct from broadcast and attempt only with a concrete research question. |

The active path is
`H1 -> H2 -> C1 -> S3 -> S4 -> D1 -> D2 -> Q1 -> Q2 -> Q3 -> R1 -> R2 -> R3 -> F1 -> F2 -> W1 -> L1A -> L1B -> L1C -> L1D -> L1E`.
A suitable physical Linux host and exact-revision execution clone have been verified, so controlled
measurement preparation can proceed. Multi-producer and general multi-consumer mechanisms remain
deliberately deferred; this does not defer single-producer broadcast/fan-out.

## Active stopping boundary

The portable single-producer path through W1 is complete. L1A is now locally executable. L1B through
L1E are ordered measurement outcomes whose detailed contracts must be written only when they become
next. M1 through M3 remain deliberately deferred until a concrete multi-producer research question
justifies reopening that path after the L1 review.

## Next stage: L1A Linux measurement integrity

### Goal

Make controlled measurements trustworthy on the verified Linux host before recording mechanism
performance. Remove benchmark-control work that can perturb a small CPU, make effective affinity a
verified fact, add explicit placement for the existing fixed two-consumer workloads, and establish
that the stock host is repeatable enough for bounded relative comparisons.

### Evidence motivating the stage

Native Linux Release and quality builds, the correctness suite, single-consumer affinity, and
generic PMU access have been validated on the host. A discovery run also showed that the benchmark
coordinator continues yielding throughout the timed phase and can occupy an otherwise unused core.
The current affinity result reports the requested CPU after a successful set operation without
reading back the effective mask. Fan-out and pipeline still reject controlled placement. Discovery
timings are plumbing and environment evidence only, not mechanism results.

### Scope

1. Replace timed-phase coordinator spinning with a small portable blocking notification while
   retaining the workload's mechanism-side `yield` waiting behavior.
2. On Linux, read back and validate the current thread affinity mask after applying a request.
   Preserve explicit unsupported behavior on macOS and keep platform-specific code localized.
3. Add the smallest explicit CLI, workload plumbing, result metadata, and validation needed to pin
   the producer and both fixed consumer roles in fan-out and pipeline runs.
4. Record the exact-revision two-machine workflow and the external host facts needed beside raw
   results without turning the benchmark binary into a topology or tuning framework.
5. Update affected tests and documentation, including planned experiment commands that name CPUs
   unavailable on the verified host.
6. After the implementation revision is committed, pushed, and green in CI, run a bounded pinned
   Release pilot on that exact revision. Retain individual trials and inspect dispersion,
   frequency/temperature behavior, and throttle counters before considering any host control.

### Non-goals

- no substantive mechanism changes or new handoff topology;
- no controlled mechanism ranking or completion of a planned performance experiment;
- no large parameter matrix, record-layout campaign, or benchmark orchestration framework;
- no mandatory in-process topology discovery or `perf` dependency;
- no persistent governor, IRQ, kernel, boot, or CPU-isolation changes;
- no MPSC, generalized sequencing, SPMC, or MPMC work.

### Acceptance criteria

- Control-plane blocking does not change queue waiting semantics, timed-region boundaries, completed
  counts, payload validation, or checksum meaning.
- Linux affinity metadata comes from a verified effective mask and rejects an unexpected mask;
  macOS continues to build and reports affinity as unsupported.
- Fan-out and pipeline accept exactly two distinct consumer CPUs, reject incomplete, duplicate, or
  producer-overlapping placement, and record requested/effective CPUs and outcomes for both roles.
- Focused platform, CLI, CSV, failure-path, and single-/multi-consumer smoke tests cover the new
  behavior.
- Debug, Release, sanitizer, format, and static-analysis gates pass where applicable on macOS,
  Linux, and required CI.
- A clean, CI-green exact revision is checked out and built natively on Linux before the pilot.
- The stock-host pilot uses explicit same-node physical cores, at least seven retained trials, and
  trials long enough to expose meaningful run-to-run variation. It records dispersion and thermal/
  frequency observations and makes no mechanism-performance claim.
- The complete diff and both worktrees are reviewed; coherent Conventional Commit(s) are pushed;
  required CI is green; ignored local status identifies the exact next stage and evidence location.
- On completion, replace this contract with one detailed L1B contract. Split L1B further rather
  than combining several weakly reviewed experiment families if its evidence scope is too large.

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
