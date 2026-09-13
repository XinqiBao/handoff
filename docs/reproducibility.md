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

Fan-out and dependency-pipeline plumbing can be exercised with:

```sh
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation fan-out --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation pipeline --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5
```

These fixed two-consumer workloads currently reject the single-consumer CPU affinity options. Do
not use their unpinned output for performance conclusions; controlled measurement requires
explicit, recorded placement for the producer and both consumers.

Fixed-record plumbing can be exercised with:

```sh
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation fixed-record --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5
./build/release/apps/handoff-bench/handoff-bench run ping-pong \
  --implementation fixed-record --payload-bytes 64 --capacity 1024 \
  --iterations 100000 --warmup 10000 --trials 5
```

These commands validate benchmark plumbing on a development host. Their timing is not controlled
performance evidence.

Variable-record byte-ring plumbing can be exercised with:

```sh
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation byte-record --payload-bytes 64 --capacity-bytes 4096 \
  --iterations 1000000 --warmup 10000 --trials 5
./build/release/apps/handoff-bench/handoff-bench run ping-pong \
  --implementation byte-record --payload-bytes 64 --capacity-bytes 4096 \
  --iterations 100000 --warmup 10000 --trials 5
```

These commands are also plumbing checks rather than controlled performance evidence. Byte-ring
capacity is native bytes and must not be reported as an equivalent fixed-slot count.

Descriptor/payload plumbing can be exercised with:

```sh
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation descriptor-record --payload-bytes 64 \
  --capacity 64 --capacity-bytes 4096 \
  --iterations 1000000 --warmup 10000 --trials 5
./build/release/apps/handoff-bench/handoff-bench run ping-pong \
  --implementation descriptor-record --payload-bytes 64 \
  --capacity 64 --capacity-bytes 4096 \
  --iterations 100000 --warmup 10000 --trials 5
```

These are plumbing checks, not performance evidence. Descriptor slots and payload bytes are
independent native limits even though the benchmark exposes only two deliberate capacity pairs.

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

CSV output records baseline run metadata when the command starts. Git fields are reported as
`unavailable` when the source checkout or Git executable cannot be queried. Effective CPU fields are
available only after a requested affinity operation succeeds. Record system tuning, topology
details, external diagnostics, and any other missing experiment-specific facts beside the CSV or in
the experiment document.

Raw local output belongs under the ignored `results/` directory by convention. Commit concise
experiment records and selected data only when they are needed to reproduce a conclusion.

## Sources of variation

Frequency scaling, thermal limits, background work, scheduler migration, topology, build flags,
payload initialization, and trial duration can dominate small mechanism differences. Record observed
conditions and distinguish them from properties of the algorithm.
