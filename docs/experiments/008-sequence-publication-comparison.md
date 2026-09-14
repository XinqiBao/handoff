# Experiment: How does explicit sequence publication compare with head/tail SPSC?

- Type: implementation comparison
- Status: planned
- Implementation revision: this record's introducing commit
- Measurement revision: to be recorded before the controlled Linux run
- Date: 2026-09-13

## Question

Under equivalent scalar SPSC throughput and ping-pong workloads, how does the explicit
claim/populate/publish and observe/release lifecycle compare with the basic head/tail ring?

## Hypothesis

Both mechanisms coordinate one producer and one consumer with two acquire/release cursors and exact
bounded backpressure, so their coordination work may be similar. The sequence mechanism also
returns move-only tokens, keeps explicit side-local next and active state, uses one-based finite
sequences, exposes const observations, and accesses slots directly in throughput. Compiler
decisions, payload size, capacity, topology, and noise may dominate any difference.

## Setup

The planned controlled Linux comparison uses a clean Release build and two verified physical cores
in one NUMA node. It covers 8, 64, and 256 byte payloads and capacities of 64 and 1024 exact usable
slots. Throughput uses 5,000,000 measured messages and ping-pong uses 500,000 measured exchanges;
both use 100,000 warmup operations and three trials.

For each workload, payload, and capacity, four commands run in ABBA order: `basic`, `sequence`,
`sequence`, `basic`. Preserve every CSV row. Median completed messages per second is the throughput
summary; the median of per-trial median RTTs is the ping-pong summary, with p95 and p99 RTT retained
as supporting observations.

## Compared implementations

- `basic` owns monotonic zero-based head/tail positions and transfers values through `try_push` and
  `try_pop` assignment;
- `sequence` owns one-based next-to-claim/observe state, publishes an atomic producer cursor, gates
  reuse with an atomic consumer sequence, and exposes explicit claim and observation tokens.

Both use default-constructed fixed slots, exact capacity, one producer, one consumer, lossless
backpressure, modulo slot reuse, yield waiting, conservative C++ acquire/release ordering, the same
payload generation and validation functions, completed operation counts, phase control, and output
schema. This is an implementation comparison rather than a one-variable mechanism isolation: token
lifecycle, local state, direct throughput slot access, and consumer value-transfer shape differ.

## Results

No controlled measurements have been collected. Local and CI smoke commands validate routing,
completion, checksum behavior, and CSV fields only. Their timings are not performance evidence.

## Interpretation

No performance conclusion is available before the controlled Linux run. Any observed difference
must be reported as a property of these two complete implementations, not as the isolated cost or
benefit of monotonic sequences or an LMAX Disruptor result.

## Limitations

The comparison does not cover multiple producers, fan-out, dependency gating, barriers, per-slot
publication, batch claims, alternative waiting strategies, cross-NUMA placement, or sequence
exhaustion during realistic runs. It does not establish compatibility with the LMAX API or Java
memory model.

## Reproduction

After verifying CPU topology and effective affinity, run:

```sh
cmake --preset release
cmake --build --preset release --parallel 4
lscpu -e=CPU,NODE,CORE,ONLINE
mkdir -p results/sequence-comparison
for workload in throughput ping-pong; do
  for payload in 8 64 256; do
    for capacity in 64 1024; do
      run=0
      for implementation in basic sequence sequence basic; do
        run=$((run + 1))
        iterations=5000000
        if [ "$workload" = ping-pong ]; then iterations=500000; fi
        taskset -c 0 ./build/release/apps/handoff-bench/handoff-bench run "$workload" \
          --implementation "$implementation" --payload-bytes "$payload" \
          --capacity "$capacity" --iterations "$iterations" --warmup 100000 --trials 3 \
          --producer-cpu 1 --consumer-cpu 2 \
          --output \
          "results/sequence-comparison/${workload}-${payload}b-${capacity}s-${run}-${implementation}.csv"
      done
    done
  done
done
```

Record the clean revision, host and toolchain metadata, CPU topology, tuning, and effective
placement. Preserve raw trials and separate observations from explanations.
