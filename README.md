# handoff

`handoff` is a C++ laboratory for studying and benchmarking bounded in-memory message handoff
mechanisms. It favors small, readable implementations that isolate ownership, publication,
sequencing, memory layout, batching, backpressure, fan-out, and contention.

This project is for controlled experiments and systems-programming study. It is not a production
IPC framework, a universal queue library, or an attempt to name one queue as universally fastest.

## Status

The repository contains readable fixed-slot bounded SPSC baselines covering head/tail, cache-line
placement, cached remote indices, batching, bulk/burst progress, staged direct-slot access,
sequence claim/publication, reliable sequence fan-out, a fixed two-stage sequence dependency
pipeline, fixed header/inline-payload records, contiguous variable-record byte storage, and split
descriptor/payload storage. A separate sequence-addressed metadata ring studies lossy broadcast
observation and detectable overwrite without pretending metadata alone is a completed message
handoff. The repository also provides equivalent correctness tests, steady-state throughput and
ping-pong round-trip latency workloads, a C++23 build, and a small platform capability layer.

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
Select `basic`, `batch`, `bulk`, `burst`, `byte-record`, `cache-line`, `cached-index`,
`descriptor-record`, `fan-out`, `fixed-record`, `pipeline`, `sequence`, or `staged` with
`--implementation`. Throughput also supports `--batch-size 1|4|16` for the `basic`, `batch`,
`bulk`, `burst`, and
`staged` implementations. `bulk`, `burst`, `fan-out`, `pipeline`, and `staged` apply only to
throughput; `fixed-record` and `sequence` are scalar in both workloads. `fan-out` and `pipeline`
each use two consumer threads and reject the single-consumer CPU affinity options. Run
`handoff-bench help` for the complete option contract.

Fixed-slot implementations use `--capacity 64|1024`. `byte-record` instead uses
`--capacity-bytes 4096|65536`; the two options are mutually exclusive.
`descriptor-record` reports both native dimensions and supports the paired capacities
`--capacity 64 --capacity-bytes 4096` and `--capacity 1024 --capacity-bytes 65536`.

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
