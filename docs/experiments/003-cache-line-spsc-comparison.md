# Experiment: Does counter separation change bounded SPSC performance?

- Type: mechanism isolation
- Status: planned
- Plumbing revision: `b63246e2d0e2fe6a61dd4a0b90774e329a01d987`
- Measurement revision: to be recorded before the controlled Linux run
- Date: 2026-09-12

## Question

Does placing the consumer-owned `head` and producer-owned `tail` counters in separate aligned state
blocks change steady-state throughput or minimum-ish ping-pong RTT under otherwise equivalent
bounded SPSC workloads?

## Hypothesis

Counter separation may improve throughput by avoiding write invalidation between adjacent atomics
when the threads run on different cores. Ping-pong is dependency-bound and may show a smaller or
different effect. Any effect should depend on payload size, capacity, cache hierarchy, topology,
and noise; neither implementation is expected to be a universal winner.

## Setup

The planned Linux comparison uses the Release preset, verified CPUs 1 and 2 on distinct physical
cores in one NUMA node, payloads of 8, 64, and 256 bytes, and exact usable capacities of 64 and 1024
slots.
Throughput uses 5,000,000 measured messages; ping-pong uses 500,000 measured exchanges. Both use
100,000 warmup operations.

For each workload/payload/capacity configuration, four three-trial commands run in ABBA order:
`basic`, `cache-line`, `cache-line`, `basic`. This retains six trial rows per implementation while
reducing a simple fixed-order confounder. Individual rows remain in separate CSV files. The primary
summaries are median messages per second for throughput and median of per-trial median RTTs for
ping-pong. P95 and p99 RTT remain supporting observations.

## Compared variants

The single intended independent variable is shared-counter placement:

- `basic`: `head` and `tail` are adjacent atomic members;
- `cache-line`: `head` and `tail` occupy separate 128-byte-aligned state blocks.

Both variants use the same inline slots, compile-time exact usable capacity, monotonic counters,
modulo addressing, `try_push`/`try_pop` operations, remote-index read on every attempt, and
relaxed-own/acquire-remote/release-publish memory orders. The benchmark dispatch instantiates the
same workload templates, so payload generation, validation, waiting, phase control, timing, and CSV
work are unchanged.

## Plumbing result

The full 24-command matrix (two workloads, two implementations, three payloads, and two capacities)
ran on the macOS development host with 10,000 measured operations, 1,000 warmup operations, and
three trials. All commands completed their internal validation. Every one of the 12 matched
basic/cache-line configurations produced identical checksums, and all CSV header and trial rows had
14 fields.

This establishes workload equivalence and result plumbing only. The ignored local smoke timings are
not experimental evidence.

## Interpretation

No performance winner is declared. macOS cannot apply the requested worker affinity, and these
sub-millisecond smoke runs are dominated by scheduler and clock effects relative to the mechanism
change. The observed numbers were intentionally not committed or interpreted.

## Limitations

The 128-byte state blocks necessarily increase object size and alignment, so object placement and
cache-set mapping remain confounders alongside the intended removal of counter co-location. The
chosen separation is conservative for common 64-byte and 128-byte cache lines but does not discover
the host line size. The experiment does not isolate cache misses or coherence events without
external counters, and it does not cover cross-NUMA placement, other waiting strategies, offered
load, or more than one producer and consumer.

Frequency scaling, thermal state, background work, command order, and timer overhead remain possible
sources of variation. Results near the measured noise floor must be reported as inconclusive.

## Reproduction

Plumbing was validated on Apple M2, macOS 26.5.1 (Darwin 25.5.0), Apple Clang 21.0.0, CMake 4.0.1,
and Ninja 1.13.2 from a clean Release build at the plumbing revision:

```sh
cmake --preset release
cmake --build --preset release --parallel 4
mkdir -p results/stage5-smoke
for workload in throughput ping-pong; do
  for payload in 8 64 256; do
    for capacity in 64 1024; do
      for implementation in basic cache-line; do
        ./build/release/apps/handoff-bench/handoff-bench run "$workload" \
          --implementation "$implementation" \
          --payload-bytes "$payload" --capacity "$capacity" \
          --iterations 10000 --warmup 1000 --trials 3 \
          --output \
          "results/stage5-smoke/${workload}-${implementation}-${payload}b-${capacity}s.csv"
      done
    done
  done
done
```

Planned controlled Linux commands use the verified same-node physical CPU 1/2 worker pair with the
blocked coordinator restricted to CPU 0:

```sh
lscpu -e=CPU,NODE,CORE,ONLINE
mkdir -p results/cache-line-comparison
for workload in throughput ping-pong; do
  case "$workload" in
    throughput) iterations=5000000 ;;
    ping-pong) iterations=500000 ;;
  esac
  for payload in 8 64 256; do
    for capacity in 64 1024; do
      run=0
      for implementation in basic cache-line cache-line basic; do
        run=$((run + 1))
        taskset -c 0 ./build/release/apps/handoff-bench/handoff-bench run "$workload" \
          --implementation "$implementation" \
          --payload-bytes "$payload" --capacity "$capacity" \
          --iterations "$iterations" --warmup 100000 --trials 3 \
          --producer-cpu 1 --consumer-cpu 2 \
          --output \
          "results/cache-line-comparison/${workload}-${payload}b-${capacity}s-${run}-${implementation}.csv"
      done
    done
  done
done
```

Before execution, record the clean revision, full toolchain and host metadata, CPU topology, system
tuning, and exact effective affinity. Retain all trial rows and report observations separately from
causal explanations.
