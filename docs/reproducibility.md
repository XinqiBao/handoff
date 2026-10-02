# Reproducibility

## Supported environments

Linux is the primary platform for controlled performance analysis. macOS supports development,
correctness validation, normal timing code, and benchmark plumbing. Windows is unsupported.

The supported toolchain is Clang with C++23, CMake 3.28 or newer, Ninja, and Conan 2.
`mise.toml` pins Clang, clang-format, clang-tidy, CMake, Ninja, Python, uv, and Conan.
Python and uv support isolated PyPI installations. First-time installation needs network access.
Clang uses the standard mise `conda:clang-22` package; `CC=clang-22` and `CXX=clang++-22`
select its drivers for CMake and Conan. This package exposes the C++ driver missing from the
default `clang` entry. Host SDK and development files remain platform prerequisites.
clang-tidy uses the standard `pypi:clang-tidy` binary package, which includes its built-in headers.
Other tools use mise's default entries.
Install `build-essential` on Ubuntu or Xcode Command Line Tools on macOS for host development
files. Record the actual compiler, SDK, and standard library when measuring.

## Tool and dependency setup

Install [mise](https://mise.jdx.dev/getting-started.html), then:

```sh
mise trust
mise install
mise exec -- conan profile detect --name handoff --force
```

The commands below assume mise is activated in the shell. Otherwise prefix each command with
`mise exec --`, as in the README. Install dependencies for the build mode before configuring CMake:

```sh
conan install . -pr:a handoff -s:a compiler.cppstd=23 -s:a build_type=Debug \
  -of build/conan/debug --build=missing
conan install . -pr:a handoff -s:a compiler.cppstd=23 -s:a build_type=Release \
  -of build/conan/release --build=missing
```

The detected `handoff` profile records the native toolchain without changing the global default
profile. The install commands select C++23 explicitly; no repository profile file is needed.
Conan automatically loads `conan.lock`, which pins dependency versions and recipe revisions;
package binaries are selected or built for the active profile. The host SDK and system libraries
remain part of the measurement environment.

Debug, correctness-only, sanitizer, and tidy presets share the Debug dependency installation;
Release uses the Release installation. Generated dependencies stay under ignored `build/`;
Conan also creates an ignored root `CMakeUserPresets.json`. When migrating an old build tree,
use `cmake --fresh --preset <name>`.

## Updating versions

Add libraries to `conanfile.txt` under `[requires]`, or `[test_requires]` for test-only dependencies,
and consume their imported CMake targets. After changing dependencies, regenerate the lock:

```sh
conan lock create . -pr:a handoff -s:a compiler.cppstd=23 --lockfile="" --lockfile-out=conan.lock
```

Reinstall dependencies and rerun the affected checks. Tool upgrades change the exact pins in
`mise.toml`. When changing Clang's major version, update its versioned package name and `CC`/`CXX`
driver names as well. Redetect the `handoff` profile and reinstall dependencies after changing
compilers. To move from C++23 to a newer baseline, change the Conan install setting, CMake's
`cxx_std_23` requirement, and benchmark's recorded standard together, then validate both platforms.

## Linux measurement capability

The [measurement host guide](measurement-host.md) defines CPU isolation, frequency
control, host qualification, and reboot handoff independently of any particular
CPU. Verified worker affinity and endpoint temperature or frequency samples
alone do not establish exclusive, uninterrupted execution. Match host controls
to the precision of the intended claim and record the live outcome. See
[benchmark methodology](benchmark-methodology.md) for evidence terms.

The historical Intel N150 [host baseline](experiments/linux-host-baseline.md)
describes the original measurements without dedicated housekeeping or IRQ isolation.
[Experiment 022](experiments/022-fixed-frequency-mpsc-comparison.md) and later qualified sessions
record their own isolation and delivered-frequency checks. Historical exact-revision observations
remain valid within their stated conditions, but do not establish fine cache or coherence cost
attribution. Semantic tests do not depend on throughput precision.

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
commit. A source-affecting change limits the old observation to its measured revision. It does not
establish performance of the new implementation; rerun only when a current question needs that evidence.

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

For mechanism-only work, the `debug-correctness` configure/build/test presets set
`HANDOFF_BUILD_BENCHMARK=OFF`. The ordinary presets retain the benchmark and its tests, including
sanitizer and static-analysis coverage. The same option is available to a manual configuration.

## Benchmark exploration and integration

`list` shows mechanisms and executable routes; `describe <route>` shows supported workloads,
capacity kind, roles, and exploratory defaults. Small mechanism-first commands use the same execution
path as explicit workload-first commands:

```sh
./build/release/apps/handoff-bench/handoff-bench list
./build/release/apps/handoff-bench/handoff-bench describe descriptor-record
./build/release/apps/handoff-bench/handoff-bench run mpsc-slot
./build/release/apps/handoff-bench/handoff-bench run smoke \
  --iterations 1000 --warmup 100 --trials 1 --output /tmp/handoff-smoke.csv
./build/release/apps/handoff-bench/handoff-bench run ping-pong \
  --implementation basic --payload-bytes 8 --capacity 64 \
  --iterations 1000 --warmup 100 --trials 2 --output /tmp/handoff-rtt-smoke.csv
```

These validate CLI, workload, timing, and output plumbing. They are not controlled performance
measurements. Use `help` for current options, the [catalog](mechanisms/README.md) for route navigation,
and [methodology](benchmark-methodology.md) for workload boundaries and special restrictions.
Historical records own their original command shapes; current CLI compatibility is retained when
useful, without a guarantee for every historical probe or raw-output schema.

Capacity is native to storage: `--capacity` means slots, `--capacity-bytes` means byte storage,
and descriptor/payload routes require both. For example:

```sh
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation descriptor-record --payload-bytes 64 \
  --capacity 64 --capacity-bytes 4096 \
  --iterations 1000 --warmup 100 --trials 1
```

An equal numeric capacity is not an equivalent message or byte capacity across layouts.
The publication-hole route reports untimed logical progress, not performance.
Offered-load publication pacing and observer stalls belong to the lossy sequence-payload workload;
they do not add a latency contract to throughput or ping-pong.

For controlled work, use explicit role placement on a qualified host. On a four-core host with CPU 0
reserved for coordinator/housekeeping, the two-producer shape is:

```sh
taskset -c 0 ./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation mpsc-slot --payload-bytes 64 --capacity 1024 \
  --iterations 20000000 --warmup 2000000 --trials 1 \
  --producer-cpus 1,2 --consumer-cpu 3 --output ROW.csv
```

For one producer/two consumers, use `--producer-cpu 1 --consumer-cpus 2,3`; list order identifies
fan-out reader 0/1, pipeline upstream/downstream, or SPMC worker 0/1. The benchmark rejects
incomplete, duplicate, or overlapping placement. SPMC timing ends after producer-verified reuse;
fan-out delivers each publication to both readers, so their rate meanings differ. Linux verifies
requested affinity exactly. macOS reports it unsupported and remains suitable for plumbing.
A command shape and placement alone do not qualify a measurement; follow the gates below.

ASan/UBSan and TSan are separate because these runtimes are not combined. TSan availability and
behavior vary by platform and toolchain; report a concrete limitation rather than weakening a valid
low-level design to obtain a clean run.

## Preparing a measurement run

For results intended to support a conclusion:

1. Use a clean native Release build at a recorded, CI-green git revision.
2. Run the correctness suite and relevant sanitizer checks first.
3. Qualify the Linux host for the intended precision using the
   [measurement host guide](measurement-host.md); record isolation and actual
   frequency behavior, not only requested settings.
4. Select CPUs explicitly and require the read-back effective masks to match exactly.
5. Keep producer and consumer on one NUMA node unless cross-node placement is intentional.
6. Run warmup and multiple trials using exact recorded commands.
7. Keep all raw trials during analysis and explain exclusions or deviations in the record.
8. Distill the question, revision/conditions, method, observations, interpretation, limits, and
   consequence into a tracked experiment record before discarding useful working material.

Exploratory work may begin on a stock host, but repeated trials or warmup cannot
make an unqualified host suitable for a stronger claim. Prepare and validate
isolation and frequency controls before controlled fine-grained comparisons.
Record and restore temporary controls; preserve the exact boot configuration
when a dedicated measurement profile is used.

Linux `perf stat` or `perf record` may be run externally when useful. Do not make perf-event access a
core executable dependency. macOS reports thread affinity as unsupported rather than attempting
Mach-specific emulation.

## Run metadata

Record:

- executable build-source revision, dirty state, and source fingerprint;
- invocation-time checkout revision and dirty state separately;
- compiler name and full version;
- CMake preset and build mode;
- OS, architecture, and CPU model;
- requested and effective producer/consumer CPUs;
- warmup, iterations, trials, and workload-specific dimensions;
- relevant system tuning and diagnostic commands.

CSV output distinguishes `build_git_revision`/`build_git_dirty` from invocation-time
`checkout_git_revision`/`checkout_git_dirty`. The build identity is refreshed by a build-time step,
including incremental builds, rather than frozen at configuration time. `build_source_sha256`
hashes sorted production files under `apps/`, `include/`, and `src/`, top-level and production
subdirectory `CMakeLists.txt`, `CMakePresets.json`, and `cmake/` files. Tests, documentation, and local results
are excluded. Build Git dirty state covers the whole checkout's tracked changes and nonignored
untracked files, including documentation; it is deliberately broader than the source fingerprint.
`build_mode`, compiler identity, and `build_flags` (CMake flags plus target compile options)
separately describe configuration. Source must remain quiescent while the build step and compiler
run; this model does not snapshot files concurrently with edits. These fields identify source/configuration, not a permanent
artifact archive or an executable hash. An old executable keeps its embedded build identity after
the checkout changes. Checkout fields query the configured source path when the command starts;
Git fields report `unavailable` when Git or the checkout cannot be queried. The former ambiguous
`git_revision`/`git_dirty` fields are deliberately replaced. Effective CPU fields are
available only after a requested affinity operation succeeds and its Linux mask is verified.
`waiting_behavior` describes mechanism-side waits; `control_waiting_behavior` separately records
the blocking harness synchronization. Record system tuning, topology
details, external diagnostics, and any other missing experiment-specific facts beside the CSV or in
the experiment document.

## Linux measurement sidecar

Keep host facts outside the benchmark binary. Beside each controlled CSV group, retain a concise
text sidecar captured immediately before the run. It should contain timestamp and hostname, exact
SHA and tracked dirty state, compiler and Release flags, kernel and OS, `lscpu`, allowed CPUs and
NUMA/cache topology, requested role placement, boot isolation and effective IRQ/workqueue masks,
governor/EPP/minimum/maximum/boost state, actual busy frequency, power limits, perf policy, load
average, relevant active processes, temperature and power observations, and thermal-throttle
counter values before and after the group. Do not capture the environment or credentials.

The Phase I [Linux measurement host baseline](experiments/linux-host-baseline.md) records the
verified N150 placement, stock policy, conditioning, and observed repeatability. Recheck these
facts before a new controlled run; a previous host observation is not a permanent tuning rule.

Raw local output belongs under ignored `results/` by convention. It is disposable working material,
not a permanent evidence source. Keep it while analysis needs it. Before cleanup, check for a useful
conclusion, invariant, method, or small input missing from tracked documentation and distill only
what matters into the existing mechanism note, experiment record, or procedure. Do not publish raw
CSV collections, full host logs, or temporary probes merely because they once informed a result.
Historical paths and formats impose no compatibility requirement on current tools.

## Sources of variation

Frequency scaling, thermal limits, background work, scheduler migration, topology, build flags,
payload initialization, and trial duration can dominate small mechanism differences. Record observed
conditions and distinguish them from properties of the algorithm.
