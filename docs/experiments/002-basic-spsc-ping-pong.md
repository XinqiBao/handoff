# Experiment: How payload and capacity affect basic SPSC round-trip latency

- Type: mechanism isolation
- Status: planned
- Plumbing revision: `6c3421980a43064e12d50ec5df95be770002f61b`
- Measurement revision: to be recorded before the controlled Linux run
- Date: 2026-09-12

## Question

How does minimum-ish ping-pong round-trip latency of the basic bounded SPSC ring change across a
small fixed set of payload sizes and usable slot capacities?

## Hypothesis

Larger payloads should raise RTT because both the request and response copy and inspect the payload.
Capacity should have little direct effect because ping-pong permits only one outstanding request,
though storage footprint and placement may still perturb cache behavior.

## Setup

The planned matrix holds the implementation (`basic`), two-ring request/response workload, one
producer, one consumer, fixed payload generation, validation, memory ordering, and per-message
publication constant. It varies payload size over 8, 64, and 256 bytes and exact usable capacity
over 64 and 1024 slots. Each configuration uses 100,000 warmup exchanges, 500,000 measured
exchanges, and seven trials. Every exchange produces one RTT sample. Each trial reports median,
p95, and p99 RTT; the cross-trial summary is the median of trial medians.

The controlled run requires a quiet Release build on Linux. CPU 2 and CPU 4 are the planned
placements under the same topology precondition as the throughput record.

## Compared variants

This is a single-mechanism characterization, not an implementation ranking. Payload bytes and slot
capacity are the independent dimensions. The benchmark workload and queue mechanism remain fixed.

## Plumbing result

The complete six-configuration matrix ran on the macOS development host with 10,000 measured
exchanges, 1,000 warmup exchanges, and three trials. Every command completed its internal sequence
and checksum validation. All 18 trial rows and their headers had the expected 14 CSV fields,
including populated median, p95, and p99 RTT fields.

These observations validate dispatch, per-exchange sampling, percentile output, validation, and CSV
plumbing only. Smoke timings were not retained as evidence.

## Interpretation

No latency interpretation is recorded. The current host cannot pin the worker threads, short trials
are scheduler-sensitive, and clock-read overhead is material at this scale.

## Limitations

RTT/2 is only a symmetry-based proxy and is not a one-way measurement. The planned experiment does
not measure latency under offered load, cross-NUMA behavior, alternative waiting, or clock overhead
in isolation. The p95 and p99 describe the sampled run but do not establish a stable service-level
bound.

## Reproduction

Plumbing used the same clean revision and Apple M2/macOS 26.5.1/Apple Clang 21.0.0/CMake 4.0.1/Ninja
1.13.2 environment recorded for the throughput experiment:

```sh
cmake --preset release
cmake --build --preset release --parallel 4
mkdir -p results/stage3-smoke
for payload in 8 64 256; do
  for capacity in 64 1024; do
    ./build/release/apps/handoff-bench/handoff-bench run ping-pong \
      --implementation basic --payload-bytes "$payload" --capacity "$capacity" \
      --iterations 10000 --warmup 1000 --trials 3 \
      --output "results/stage3-smoke/ping-pong-${payload}b-${capacity}s.csv"
  done
done
```

Planned controlled Linux commands, conditional on confirming CPUs 2 and 4 are online, on distinct
physical cores, and in one NUMA node:

```sh
lscpu -e=CPU,NODE,CORE,ONLINE
mkdir -p results/basic-ping-pong
for payload in 8 64 256; do
  for capacity in 64 1024; do
    ./build/release/apps/handoff-bench/handoff-bench run ping-pong \
      --implementation basic --payload-bytes "$payload" --capacity "$capacity" \
      --iterations 500000 --warmup 100000 --trials 7 \
      --producer-cpu 2 --consumer-cpu 4 \
      --output "results/basic-ping-pong/${payload}b-${capacity}s.csv"
  done
done
```
