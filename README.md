# handoff

`handoff` is a C++ laboratory for studying and benchmarking bounded in-memory message handoff
mechanisms. It favors small, readable implementations that isolate ownership, publication,
sequencing, memory layout, batching, backpressure, fan-out, and contention.

This project is for controlled experiments and systems-programming study. It is not a production
IPC framework, a universal queue library, or an attempt to name one queue as universally fastest.

## Status

The repository contains readable fixed-slot bounded SPSC baselines covering head/tail, cache-line
placement, cached remote indices, batching, bulk/burst progress, staged direct-slot access, and
sequence claim/publication. It also provides equivalent correctness tests, steady-state throughput
and ping-pong round-trip latency workloads, a C++23 build, and a small platform capability layer.

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
```

The `smoke` command checks timing, CLI, and result-output plumbing. Its timing is not a handoff
performance result.

The SPSC commands support optional `--producer-cpu`, `--consumer-cpu`, and `--output` arguments.
Select `--implementation basic|batch|bulk|burst|cache-line|cached-index|sequence|staged`; throughput
also supports `--batch-size 1|4|16` for the `basic`, `batch`, `bulk`, `burst`, and `staged`
implementations. `bulk`, `burst`, and `staged` apply only to throughput; `sequence` is scalar in both
workloads. Run `handoff-bench help` for the complete option contract.

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
