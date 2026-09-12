# Roadmap

This roadmap expresses technical order, not dates or commitments. Later stages may change when
earlier correctness work and experiments reveal better questions.

## Current

Bootstrap infrastructure and the initial SPSC layout mechanisms are complete:

- C++23 Clang/CMake/Ninja build presets;
- Catch2 test integration and warning, format, tidy, ASan/UBSan, and TSan paths;
- portable system summary plus optional Linux current-thread affinity;
- explicit `handoff-bench` CLI with a non-mechanism smoke workload and CSV plumbing;
- benchmark, architecture, design-space, and reproducibility conventions;
- lightweight Linux/macOS CI validation;
- a fixed-slot bounded SPSC baseline with compile-time exact usable capacity, non-blocking
  operations, conservative acquire/release publication, and bounded-FIFO correctness coverage;
- steady-state throughput and ping-pong RTT workloads with warmup, multiple trials, validation,
  median summaries, CSV output, and optional CPU affinity;
- planned bounded baseline throughput and ping-pong experiments, with macOS plumbing validation and
  controlled Linux execution intentionally pending;
- a cache-line-separated SPSC variant with equivalent correctness coverage and shared benchmark
  workloads, preserving the basic baseline unchanged;
- a prepared mechanism-isolation comparison of adjacent versus separated counters, with the full
  paired matrix smoke-validated on macOS and performance execution pending on controlled Linux.

## Near term

1. **Controlled Linux comparison**: execute the prepared cache-layout record with confirmed
   same-NUMA, distinct-core affinity and retain all trial rows before drawing a conditional result.
2. **Memory-order refinement**: vary ordering only where a written happens-before argument permits.
3. **Cached remote indices**: isolate reduced shared-index reads from layout and ordering changes.
4. **Batching**: study per-message versus batched publication under controlled workloads.

Each mechanism must have a local note and appropriate correctness tests before comparison.

## Later

- sequence-based publication, producer cursors, and consumer gating;
- Disruptor-inspired independent consumers, dependency graphs, and fan-out;
- fixed header-plus-payload slot layouts;
- variable record byte rings with alignment, padding records, commit, and contiguous wrap handling;
- descriptor rings with separate payload storage;
- Firedancer-inspired metadata/data separation, sequence-addressed metadata, chunk-addressed payload,
  consumer progress, overrun detection, and explicit lossy broadcast semantics;
- DPDK-inspired SP/SC head reservation, publication, bulk/burst, and staged
  reserve/write/finish operations;
- bounded MPSC and selected multi-producer sequencing or synchronization ideas.

These are simplified educational mechanisms, not compatibility projects.

## Exploratory

- SPMC work-sharing variants distinct from broadcast;
- selected multi-producer availability tracking;
- MPMC mechanisms after simpler topologies establish useful questions;
- producer/consumer imbalance, temporary stalls, and offered-load latency;
- selected reference implementation comparisons added only on demand.

## Stage completion criteria

A stage is complete when its semantics and non-goals are documented, correctness tests cover the
relevant common and mechanism-specific invariants, required checks pass, benchmark claims match the
measurement method, the complete diff is reviewed, and a coherent commit is pushed. Merely compiling
does not complete a concurrency stage.
