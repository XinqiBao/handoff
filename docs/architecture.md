# Architecture

## Purpose

`handoff` isolates the semantics and costs of bounded in-memory message handoff mechanisms. The
architecture supports controlled experiments without requiring a framework around each algorithm.

Readability is a design constraint. A mechanism should normally be understandable from its own
code, its mechanism note, and a small set of common utilities. Some duplication is acceptable when
it makes variants easier to compare.

## System boundaries

In scope are bounded queues and rings, ownership transfer, publication, progress tracking,
backpressure, overwrite detection, batching, fan-out, waiting, and memory ordering. The laboratory
may study SPSC, MPSC, SPMC, and MPMC topologies over time.

Networking, distributed queues, production IPC orchestration, shared-memory lifecycle management,
process discovery, crash recovery, shared-memory allocators, general task scheduling, allocator
benchmarks, Windows, and NUMA as a primary research subject are outside the foreseeable scope.

Algorithms should not gratuitously prevent later placement in shared memory, but no shared-memory
process infrastructure is planned.

## Responsibilities

- **Mechanisms** own representation, concurrency contracts, and operations. They must not depend on
  the benchmark harness.
- **Benchmark code** owns workloads, phases, validation, timing, summaries, and result output. It
  adapts directly to the small set of mechanisms under study.
- **Platform code** exposes only narrow optional capabilities such as system identification and
  current-thread CPU affinity.
- **Tests** establish common bounded-FIFO invariants and mechanism-specific semantics according to
  the [testing strategy](testing-strategy.md) before a mechanism enters comparisons.
- **Documentation** records intent, exact mechanism semantics, experiment questions, and provenance.

Dependencies should point from executables and tests toward mechanisms and small utilities, never
from mechanisms toward the harness. No common abstract queue base class is planned: mechanisms may
have different ownership, reservation, broadcast, or loss semantics.

## Source organization

Code is organized primarily by mechanism. Expected categories include `spsc`, `sequence`, `record`,
`descriptor`, `benchmark`, and `platform`, introduced only when code exists. Directories named after
LMAX, Firedancer, or DPDK would obscure which property is actually being studied; those systems
belong in [inspiration notes](inspirations/README.md).

Current targets are deliberately small:

- `handoff_core`: version and platform capabilities, with header-only mechanism implementations;
- `handoff-bench`: explicit CLI, smoke plumbing, and SPSC throughput and ping-pong workloads;
- `handoff_tests`: Catch2-based correctness checks.

Catch2 is fetched only for test-enabled builds and pinned to a commit. The project does not use a
general package manager.

## Benchmark data flow

A benchmark command validates its options, constructs state, creates and places threads, performs
warmup, synchronizes the timed phase, validates observable work, and only then formats or writes
results. The timed-region exclusions and result fields are defined in
[Benchmark Methodology](benchmark-methodology.md).

The harness should remain switch- or table-driven while the implementation set is small. A plugin
system, benchmark DSL, reflection layer, polymorphic configuration hierarchy, or general factory is
not justified.

## Platform boundary

Core algorithms, tests, workloads, and `std::chrono::steady_clock` timing should work on Linux and
macOS where practical. Linux may add current-thread affinity, `perf` helper scripts, and richer host
metadata. These remain optional capabilities or external tools.

macOS need not emulate Linux affinity through a complex Mach layer. Unsupported requests should be
reported clearly and should continue when the requested experiment remains meaningful.

## Evolution rules

- Preserve educational baselines instead of repeatedly mutating one implementation into an opaque
  optimized version.
- Isolate one important property at a time in mechanism-isolation experiments.
- Add abstractions only after repeated code demonstrates that they clarify rather than hide the
  mechanism.
- Keep storage and publication semantics explicit; avoid premature universal APIs.
- Let evidence from earlier experiments revise later roadmap stages.
