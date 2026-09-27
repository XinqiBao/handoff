# Reproducibility

## Supported environments

Linux is the primary platform for controlled performance analysis. macOS supports development,
correctness validation, normal timing code, and benchmark plumbing. Windows is unsupported.

The supported toolchain is Clang with C++23, CMake 3.28 or newer, and Ninja. The first test-enabled
configuration needs network access and Git to fetch the pinned Catch2 revision. clang-format and
clang-tidy are needed only for their corresponding checks.

## Current Linux host capability

The recorded Intel N150 measurement host has four physical cores and no SMT. With three workers and
a coordinator, all cores are occupied even when the coordinator is restricted to CPU 0. The stock
OS still runs other processes and handles interrupts and softirqs; the repository does not establish
dedicated IRQ or housekeeping isolation. Recent producer- and consumer-coordination experiments
recorded run-to-run variability, and perf-event access is restricted on this host. Verified worker
affinity and endpoint temperature/frequency checks establish placement and useful context, not
uninterrupted per-trial execution or a fine cache/coherence cost breakdown.

This host remains useful for correctness, benchmark plumbing, progress diagnostics, and conditional
complete-route observations under recorded procedures. Serious fine-grained attribution requires a
separately prepared measurement environment and an explicit question. Existing exact-SHA
observations remain valid within their recorded conditions; their semantic tests do not depend on
throughput precision. See [benchmark methodology](benchmark-methodology.md) for evidence terms.

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
8. Make all source, experiment-record, and research-direction changes in the authoritative
   checkout, then repeat the commit, CI, and exact-SHA cycle.

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

The same placement form applies to `spmc-serialized`, `spmc-ordered`, and
`spmc-slot` throughput. These deliver each publication to one worker and end
timing after the producer verifies the final reusable prefix. For example:

```sh
taskset -c 0 ./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation spmc-slot --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5 \
  --producer-cpu 1 --consumer-cpus 2,3
```

The two-producer throughput comparison can be exercised with:

```sh
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation mpsc-serialized --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5 \
  --producer-cpus 1,2 --consumer-cpu 3
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation mpsc-ordered --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5 \
  --producer-cpus 1,2 --consumer-cpu 3
./build/release/apps/handoff-bench/handoff-bench run publication-hole \
  --payload-bytes 64 --capacity 1024
# Independent producer completion and consumer slot discovery use the same shapes:
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation mpsc-count --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5 \
  --producer-cpus 1,2 --consumer-cpu 3
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation mpsc-slot --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5 \
  --producer-cpus 1,2 --consumer-cpu 3
./build/release/apps/handoff-bench/handoff-bench run publication-hole \
  --implementation mpsc-count --payload-bytes 64 --capacity 64
./build/release/apps/handoff-bench/handoff-bench run publication-hole \
  --implementation mpsc-slot --payload-bytes 64 --capacity 64
```

For a controlled run on the historical four-core host, restrict the coordinator to CPU 0 and
recheck current placement, interference, and repeatability first. The publication-hole command
reports logical progress counts only; its output is not timing evidence.

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

The Phase I [Linux measurement host baseline](experiments/linux-host-baseline.md) records the
verified N150 placement, stock policy, conditioning, and observed repeatability. Recheck these
facts before a new controlled run; a previous host observation is not a permanent tuning rule.

Raw local output belongs under the ignored `results/` directory by convention. Commit concise
experiment records and selected data only when they are needed to reproduce a conclusion.

## Sources of variation

Frequency scaling, thermal limits, background work, scheduler migration, topology, build flags,
payload initialization, and trial duration can dominate small mechanism differences. Record observed
conditions and distinguish them from properties of the algorithm.
