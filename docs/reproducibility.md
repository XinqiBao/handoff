# Reproducibility

## Supported environments

Linux is the primary platform for controlled performance analysis. macOS supports development,
correctness validation, normal timing code, and benchmark plumbing. Windows is unsupported.

The supported toolchain is Clang with C++23, CMake 3.28 or newer, and Ninja. The first test-enabled
configuration needs network access and Git to fetch the pinned Catch2 revision. clang-format and
clang-tidy are needed only for their corresponding checks.

## Build presets

```sh
# Development
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --no-tests=error

# Optimized build
cmake --preset release
cmake --build --preset release
ctest --preset release --no-tests=error

# Dynamic analysis
cmake --preset asan-ubsan
cmake --build --preset asan-ubsan
ctest --preset asan-ubsan --no-tests=error

cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan --no-tests=error

# Static analysis and formatting
cmake --preset tidy
cmake --build --preset tidy
cmake --build --preset tidy --target format-check
ctest --preset tidy --no-tests=error
```

ASan/UBSan and TSan are separate because these runtimes are not combined. TSan availability and
behavior vary by platform and toolchain; report a concrete limitation rather than weakening a valid
low-level design to obtain a clean run.

## Benchmark smoke check

```sh
./build/release/apps/handoff-bench/handoff-bench list
./build/release/apps/handoff-bench/handoff-bench run smoke \
  --iterations 1000 --warmup 100 --trials 1 \
  --output /tmp/handoff-smoke.csv
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation basic --payload-bytes 8 --capacity 64 \
  --iterations 1000 --warmup 100 --trials 2 \
  --output /tmp/handoff-throughput-smoke.csv
./build/release/apps/handoff-bench/handoff-bench run ping-pong \
  --implementation basic --payload-bytes 8 --capacity 64 \
  --iterations 1000 --warmup 100 --trials 2 \
  --output /tmp/handoff-ping-pong-smoke.csv
```

These small runs confirm CLI, timing, queue-workload validation, and CSV plumbing only. Their
timings are not performance evidence.

## Preparing a measurement run

For results intended to support a conclusion:

1. Use a clean Release build at a recorded git revision.
2. Run the correctness suite and relevant sanitizer checks first.
3. Minimize unrelated system activity and power-management changes.
4. On Linux, select CPUs explicitly and confirm effective affinity.
5. Keep producer and consumer on one NUMA node unless cross-node placement is intentional.
6. Run warmup and multiple trials using exact recorded commands.
7. Preserve raw trials and explain exclusions or deviations.

Linux `perf stat` or `perf record` may be run externally when useful. Do not make perf-event access a
core executable dependency. macOS reports thread affinity as unsupported rather than attempting
Mach-specific emulation.

## Run metadata

Record:

- git revision and whether the working tree was clean;
- compiler name and full version;
- CMake preset and build mode;
- OS, architecture, and CPU model;
- requested and effective producer/consumer CPUs;
- warmup, iterations, trials, and workload-specific dimensions;
- relevant system tuning and diagnostic commands.

The bootstrap system summary is intentionally small and does not yet collect all metadata. Until
the harness grows, record missing items beside the CSV or in the experiment document.

Raw local output belongs under the ignored `results/` directory by convention. Commit concise
experiment records and selected data only when they are needed to reproduce a conclusion.

## Sources of variation

Frequency scaling, thermal limits, background work, scheduler migration, topology, build flags,
payload initialization, and trial duration can dominate small mechanism differences. Record observed
conditions and distinguish them from properties of the algorithm.
