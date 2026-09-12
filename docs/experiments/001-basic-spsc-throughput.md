# Experiment: How payload and capacity affect basic SPSC throughput

- Type: mechanism isolation
- Status: planned
- Plumbing revision: `6c3421980a43064e12d50ec5df95be770002f61b`
- Measurement revision: to be recorded before the controlled Linux run
- Date: 2026-09-12

## Question

How does completed-message throughput of the basic bounded SPSC ring change across a small fixed
set of payload sizes and usable slot capacities?

## Hypothesis

Larger payloads should reduce messages per second because payload generation, copying, and checksum
work increase. A capacity of 1024 slots may absorb more short scheduling imbalance than 64 slots,
but capacity alone is not expected to determine throughput on a quiet, pinned system.

## Setup

The planned matrix holds the implementation (`basic`), one producer, one consumer, fixed payload
generation, validation, memory ordering, and per-message publication constant. It varies payload
size over 8, 64, and 256 bytes and exact usable capacity over 64 and 1024 slots. Each configuration
uses 100,000 warmup messages, 5,000,000 measured messages, and seven trials. The summary statistic is
the median completed messages per second; all trial rows are retained.

The controlled run requires a quiet Release build on Linux. CPU 2 and CPU 4 are the planned
producer and consumer placements, but the record must be revised before execution if `lscpu` does
not show that they are online, on distinct physical cores, and in one NUMA node.

## Compared variants

This is a single-mechanism characterization, not an implementation ranking. Payload bytes and slot
capacity are the independent dimensions. The benchmark workload and queue mechanism remain fixed.

## Plumbing result

The complete six-configuration matrix ran on the macOS development host with 10,000 measured
messages, 1,000 warmup messages, and three trials. Every command completed its internal sequence and
checksum validation. All 18 trial rows and their headers had the expected 14 CSV fields. An
additional run requesting CPUs 0 and 1 reported affinity as unsupported and continued cleanly.

These observations validate dispatch, timing phases, result validation, affinity fallback, and CSV
output only. Smoke timings were left under ignored `results/` storage and are not experimental
evidence.

## Interpretation

No throughput interpretation is recorded. The current host cannot pin the worker threads, and the
short smoke duration is intentionally unsuitable for performance conclusions.

## Limitations

The planned experiment covers one Linux CPU placement, two capacities, three payload sizes, and one
steady-state workload. It does not study topology, NUMA crossings, burst behavior,
producer/consumer imbalance, stalls, waiting strategy, or tail latency. Payload generation and
validation remain part of the benchmark-side work, so results do not isolate queue instructions.

## Reproduction

Plumbing was validated on Apple M2, macOS 26.5.1 (Darwin 25.5.0), Apple Clang 21.0.0, CMake 4.0.1,
and Ninja 1.13.2 from a clean Release build at the plumbing revision:

```sh
cmake --preset release
cmake --build --preset release --parallel 4
mkdir -p results/stage3-smoke
for payload in 8 64 256; do
  for capacity in 64 1024; do
    ./build/release/apps/handoff-bench/handoff-bench run throughput \
      --implementation basic --payload-bytes "$payload" --capacity "$capacity" \
      --iterations 10000 --warmup 1000 --trials 3 \
      --output "results/stage3-smoke/throughput-${payload}b-${capacity}s.csv"
  done
done
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation basic --payload-bytes 8 --capacity 64 \
  --iterations 10000 --warmup 1000 --trials 3 \
  --producer-cpu 0 --consumer-cpu 1 \
  --output results/stage3-smoke/throughput-affinity-unsupported.csv
```

Planned controlled Linux commands, conditional on confirming the stated CPU topology:

```sh
lscpu -e=CPU,NODE,CORE,ONLINE
mkdir -p results/basic-throughput
for payload in 8 64 256; do
  for capacity in 64 1024; do
    ./build/release/apps/handoff-bench/handoff-bench run throughput \
      --implementation basic --payload-bytes "$payload" --capacity "$capacity" \
      --iterations 5000000 --warmup 100000 --trials 7 \
      --producer-cpu 2 --consumer-cpu 4 \
      --output "results/basic-throughput/${payload}b-${capacity}s.csv"
  done
done
```
