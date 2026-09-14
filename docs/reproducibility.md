# Reproducibility

## Supported environments

Linux is the primary platform for controlled performance analysis. macOS supports development,
correctness validation, normal timing code, and benchmark plumbing. Windows is unsupported.

The supported toolchain is Clang with C++23, CMake 3.28 or newer, and Ninja. The first test-enabled
configuration needs network access and Git to fetch the pinned Catch2 revision. clang-format and
clang-tidy are needed only for their corresponding checks.

## Two-machine revision workflow

When development and controlled measurement use different machines, keep one authoritative
development checkout and treat the Git remote plus an exact commit as the provenance boundary:

1. Make and validate source or documentation changes in the authoritative development checkout.
2. Review, commit, push, and wait for the required CI checks.
3. Record the exact CI-green commit SHA selected for measurement.
4. In an independent Linux execution clone, require a clean tracked/index state, fetch the remote,
   and check out that exact SHA in detached-HEAD state.
5. Configure and build natively on Linux; never copy a development-host binary or build tree.
6. Run the required correctness gates before controlled measurement.
7. Keep raw results in ignored local storage and transfer them one way to the development machine
   for analysis when needed.
8. Make all source, experiment-record, and roadmap changes in the authoritative checkout, then
   repeat the commit, CI, and exact-SHA cycle.

Do not use a shared network build tree or bidirectional source synchronization for formal results.
Do not describe an execution clone as "latest main" in an experiment record: identify the measured
commit. A source-affecting change after measurement invalidates affected evidence until it is rerun.

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
  --iterations 1000000 --warmup 10000 --trials 5 \
  --producer-cpu 1 --consumer-cpus 2,3
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation pipeline --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5 \
  --producer-cpu 1 --consumer-cpus 2,3
```

Run controlled forms under `taskset -c 0` so the blocked coordinator and unpinned process work stay
on CPU 0. Consumer list order maps to fan-out consumer 0/1 and pipeline upstream/downstream. The
benchmark rejects incomplete, duplicate, or producer-overlapping multi-consumer placement.

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

Sequence-payload offered-load plumbing can be exercised in three deliberate shapes:

```sh
# Unpaced pressure
./build/release/apps/handoff-bench/handoff-bench run offered-load \
  --implementation sequence-payload --payload-bytes 64 --capacity 1024 \
  --producer-interval-ns 0 --iterations 1000000 --warmup 10000 --trials 5

# Paced producer
./build/release/apps/handoff-bench/handoff-bench run offered-load \
  --implementation sequence-payload --payload-bytes 64 --capacity 1024 \
  --producer-interval-ns 1000 --iterations 1000000 --warmup 10000 --trials 5

# Periodic observer stalls
./build/release/apps/handoff-bench/handoff-bench run offered-load \
  --implementation sequence-payload --payload-bytes 64 --capacity 1024 \
  --producer-interval-ns 1000 --consumer-stall-every 1024 \
  --consumer-stall-ns 100000 --iterations 1000000 --warmup 10000 --trials 5
```

These command shapes are not controlled performance evidence when run on macOS, a development
host, or a GitHub-hosted runner. Controlled conclusions require the Linux placement and host
controls described below.

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

1. Use a clean native Release build at a recorded, CI-green git revision.
2. Run the correctness suite and relevant sanitizer checks first.
3. Minimize unrelated system activity and power-management changes.
4. On Linux, select CPUs explicitly and require the read-back effective masks to match exactly.
5. Keep producer and consumer on one NUMA node unless cross-node placement is intentional.
6. Run warmup and multiple trials using exact recorded commands.
7. Preserve raw trials and explain exclusions or deviations.

Begin with the stock host. First measure repeatability, then identify a concrete source of material
variation, form a hypothesis, apply the smallest reversible control, and remeasure. Record and
restore temporary controls. Persistent kernel, boot, CPU-isolation, IRQ, or power-policy changes are
not default benchmark preparation.

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
available only after a requested affinity operation succeeds and its Linux mask is verified.
`waiting_behavior` describes mechanism-side waits; `control_waiting_behavior` separately records
the blocking harness synchronization. Record system tuning, topology
details, external diagnostics, and any other missing experiment-specific facts beside the CSV or in
the experiment document.

## Linux measurement sidecar

Keep host facts outside the benchmark binary. Beside each controlled CSV group, retain a concise
text sidecar captured immediately before the run. It should contain timestamp and hostname, exact
SHA and tracked dirty state, compiler and Release flags, kernel and OS, `lscpu`, allowed CPUs and
NUMA/cache topology, requested role placement, governor/EPP/minimum/maximum/boost state, perf policy,
load average, relevant active processes, temperature/frequency observations, and thermal-throttle
counter values before and after the group. Do not capture the environment or credentials.

For the verified four-core N150 host, the canonical two-worker placement is coordinator CPU 0,
producer CPU 1, and consumer CPU 2. Fixed three-worker workloads add consumer CPU 3 and must note
that CPU 3 has more historical network softirq activity. Keep the stock `intel_pstate` policy unless
measured instability justifies a temporary, recorded, and restored control.

### Verified stock-host baseline

On 2026-09-14, commit `273c757109e9b1650ef94938d6086e32dad495bd` passed the Linux Release,
format, clang-tidy, ASan/UBSan, and TSan gates before a pinned reproducibility pilot on the N150
host. The benchmark process was restricted to coordinator CPU 0, with producer CPU 1 and consumer
CPU 2 verified from their effective masks. The workload was basic throughput with a 64-byte
payload, 1024 slots, 20,000,000 measured messages, 2,000,000 warmup messages, and seven retained
trials. Every timed trial exceeded 2.4 seconds.

The first group had a median of 8.062 million messages/s, sample CV 0.937%, and a 2.305% full range.
Because its first-to-last change was -1.129% while the package warmed, a second group was retained
after a longer 40,000,000-message conditioning run. The warm-state repetition had a median of 8.068
million messages/s, sample CV 0.280%, 0.915% full range, +0.269% first-to-last change, and a fitted
slope of +0.025% of the median per trial. Turbostat observed roughly 3.4-3.6 GHz busy frequency and
70-78 C during the warm group. Hardware thermal-throttle counters did not change.

No governor, EPP, perf policy, IRQ, kernel, or boot setting was changed: the run used the stock
`intel_pstate` `powersave` governor with `balance_performance` EPP. The result supports using this
host for bounded relative comparisons after consistent warm-state conditioning. It is not a
mechanism-performance claim, and effects close to the observed dispersion still require cautious
interpretation. Raw CSV, sidecars, runtime affinity checks, turbostat output, and analysis remain in
ignored local storage at `results/l1/l1a-baseline/` on both machines.

Raw local output belongs under the ignored `results/` directory by convention. Commit concise
experiment records and selected data only when they are needed to reproduce a conclusion.

## Sources of variation

Frequency scaling, thermal limits, background work, scheduler migration, topology, build flags,
payload initialization, and trial duration can dominate small mechanism differences. Record observed
conditions and distinguish them from properties of the algorithm.
