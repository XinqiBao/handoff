# handoff

`handoff` is a C++ research laboratory for bounded in-memory message handoff. It favors small,
readable implementations that isolate ownership, publication, sequencing, memory layout, batching,
backpressure, fan-out, and contention.

This project is for controlled experiments and systems-programming study. It is not a production
IPC framework, a universal queue library, or an attempt to name one queue as universally fastest.

## Status

The repository contains readable fixed-slot bounded SPSC baselines covering head/tail, cache-line
placement, cached remote indices, batching, bulk/burst progress, staged direct-slot access,
sequence claim/publication, reliable sequence fan-out, a fixed two-stage sequence dependency
pipeline, fixed header/inline-payload records, contiguous variable-record byte storage, and split
descriptor/payload storage. A separate sequence-addressed metadata ring studies lossy broadcast
observation and detectable overwrite; a chunk-addressed extension coordinates the same publication
state with copied payload bytes. An offered-load workload reports how many such publications are
observed or overwritten under producer pacing and temporary observer stalls. The repository also
provides equivalent correctness tests, steady-state throughput and ping-pong round-trip latency
workloads, a C++23 build, and a small platform capability layer.

The initial single-producer research phase and selected Linux comparisons are complete.
The producer-completion study compares the serialized control and ordered-publication ring with
two concurrent-claim MPSC designs: a cooperative completion count and per-slot generation-tagged
availability. Its publication-hole diagnostic separates returned calls from consumer-visible
progress. The [research direction](docs/roadmap.md) records the current questions and limits.

The consumer-coordination study adds a one-producer work-sharing family:
serialized consumer ownership, shared claims with ordered release, and shared
claims with per-slot completion and producer-discovered reuse. Each published
position has one owner. Its complete-handoff throughput mode uses two workers;
three-owner release holes remain untimed tests.

The ordered worker-stage ring adds two competing mutable workers and one ordered downstream
consumer. Workers complete independently; downstream discovers their contiguous completed prefix,
and its final release alone gates physical reuse. The [mechanism note](docs/mechanisms/ordered-worker-stage.md)
and [Experiment 018](docs/experiments/018-ordered-worker-stage.md) record the lifecycle and evidence.

The topology study adds two producer-owned SPSC paths with consumer-controlled merge selection and
a fixed two-branch write/join. The first localizes backpressure and permits only per-producer FIFO;
the second requires both branch results before join observation and gates reuse on final join
release. [Experiments 019 and 020](docs/experiments/README.md) record the semantic evidence and
limits; neither makes a performance claim.

The variable-record MPSC study pairs descriptor order with variable byte-range reservations. Two
producers may write and finish independently after admission; one FIFO consumer releases each
record's descriptor and complete byte extent only after its final read. The
[mechanism note](docs/mechanisms/mpsc-variable-record.md) and
[Experiment 021](docs/experiments/021-mpsc-variable-record.md) record the semantic tests and limits.

## Requirements

- Clang with C++23 support
- CMake 3.28 or newer
- Ninja
- Git for fetching the pinned Catch2 test dependency
- clang-format and clang-tidy for the optional quality checks

Linux is the primary performance-analysis platform. macOS is supported for normal development,
correctness tests, and benchmark plumbing. Platform-specific capabilities degrade explicitly when
they are unavailable.

## Build and test

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --no-tests=error
```

The first test-enabled configuration fetches Catch2 at a pinned commit. Release and sanitizer
presets are documented in [Reproducibility](docs/reproducibility.md).

## Benchmark skeleton

```sh
./build/debug/apps/handoff-bench/handoff-bench list
./build/debug/apps/handoff-bench/handoff-bench run smoke \
  --iterations 1000 --warmup 100 --trials 1
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation batch --payload-bytes 64 --capacity 1024 --batch-size 4 \
  --iterations 1000000 --warmup 10000 --trials 5
./build/release/apps/handoff-bench/handoff-bench run ping-pong \
  --implementation basic --payload-bytes 64 --capacity 1024 \
  --iterations 100000 --warmup 10000 --trials 5
./build/release/apps/handoff-bench/handoff-bench run offered-load \
  --implementation sequence-payload --payload-bytes 64 --capacity 1024 \
  --producer-interval-ns 0 --consumer-stall-every 0 --consumer-stall-ns 0 \
  --iterations 1000000 --warmup 10000 --trials 5
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation mpsc-ordered --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5
./build/release/apps/handoff-bench/handoff-bench run publication-hole \
  --implementation mpsc-slot --payload-bytes 64 --capacity 64
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation spmc-slot --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5
```

The `smoke` command checks timing, CLI, and result-output plumbing. Its timing is not a handoff
performance result.

Run `handoff-bench help` for the current option and workload contract. The
[mechanism index](docs/mechanisms/README.md) maps implementations to their exact semantics;
[benchmark methodology](docs/benchmark-methodology.md) defines what each result means, and
[reproducibility](docs/reproducibility.md) covers placement and measurement procedure. Linux verifies
each requested worker affinity mask; macOS reports affinity as unsupported. Development-host and CI
timings validate plumbing, not comparative performance.

## Documentation

- [Architecture](docs/architecture.md)
- [Design space](docs/design-space.md)
- [Testing strategy](docs/testing-strategy.md)
- [Benchmark methodology](docs/benchmark-methodology.md)
- [Reproducibility](docs/reproducibility.md)
- [Roadmap](docs/roadmap.md)
- [Mechanism notes](docs/mechanisms/README.md)
- [External inspirations](docs/inspirations/README.md)
- [Experiment records](docs/experiments/README.md)
