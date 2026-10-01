# handoff

`handoff` is a C++23 collection of independent bounded in-memory handoff mechanisms. Each
isolates a question about ownership, publication, completion, reuse, storage, broadcast, or
contention. The collection is for systems study and controlled experiments: it is not a production
IPC framework, a universal queue library, or a claim that one design is always fastest.

Start at the [mechanism catalog](docs/mechanisms/README.md). It groups all assets by contract,
offers conceptual reading paths, and links exact notes, source, tests, evidence, and runnable
routes. The [design space](docs/design-space.md) defines terms. Experiment numbers record research
history, not a required reading order.

## Build

Requires Clang with C++23 support, CMake 3.28+, Ninja, and Git for the pinned Catch2 dependency.
Linux is the primary performance-analysis platform; macOS supports normal development, correctness
tests, and benchmark plumbing. Optional quality checks need clang-format and clang-tidy.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --no-tests=error
```

For a correctness-only build that omits benchmark compilation, use the `debug-correctness` configure,
build, and test presets. The first test-enabled configuration fetches pinned Catch2. Release and
sanitizer procedures are in
[reproducibility](docs/reproducibility.md).

## Explore

```sh
./build/debug/apps/handoff-bench/handoff-bench list
./build/debug/apps/handoff-bench/handoff-bench describe mpsc-slot
./build/debug/apps/handoff-bench/handoff-bench run mpsc-slot
```

`list` distinguishes all assets, executable routes, and the harness-only `smoke` workload.
An asset name with one route also runs directly; an asset with multiple routes asks for a route
name. Mechanism-first runs use small exploratory defaults. For controlled comparisons use an
explicit configuration and follow [benchmark methodology](docs/benchmark-methodology.md) and
[reproducibility](docs/reproducibility.md). Historical workload-first commands remain valid:

```sh
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation mpsc-slot --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5
```

`handoff-bench help` lists options. `smoke` checks harness plumbing; its timing is not mechanism
performance evidence. The publication-hole route is an untimed diagnostic. Linux verifies each
requested worker affinity mask; macOS reports affinity as unsupported.

## Reference

- [Architecture](docs/architecture.md): code boundaries and dependency direction.
- [Mechanism catalog](docs/mechanisms/README.md): all assets, contracts, source, tests, routes.
- [Experiment records](docs/experiments/README.md): questions, chronology, evidence, limits.
- [Roadmap](docs/roadmap.md): current research direction and open regions.
- [Testing strategy](docs/testing-strategy.md): correctness obligations.
- [Benchmark methodology](docs/benchmark-methodology.md): workload meaning and fair comparisons.
- [Linux measurement host](docs/measurement-host.md): CPU isolation, frequency control, and
  qualification.
- [Reproducibility](docs/reproducibility.md): builds, host controls, exact procedures.
- [External inspirations](docs/inspirations/README.md): ideas and primary-source provenance.

Licensed under the [MIT License](LICENSE).
