# Experiment: How does best-effort burst progress differ from fixed bulk progress?

- Type: mechanism isolation
- Status: planned
- Implementation revision: this record's introducing commit
- Measurement revision: to be recorded before the controlled Linux run
- Date: 2026-09-13

## Question

For equal requested SPSC message groups, how does best-effort burst progress differ from
all-or-nothing bulk progress when temporary free-space or data availability is smaller than the
request?

## Hypothesis

Burst operations may avoid waiting for a complete group by publishing or consuming a smaller
prefix, while bulk operations may use fewer successful calls and preserve fixed publication
granularity. Which behavior matters may depend on group size, capacity, payload size, relative
producer-consumer progress, and system noise.

## Setup

The planned controlled Linux comparison uses a clean Release build and two verified physical cores
in one NUMA node. It covers 8, 64, and 256 byte payloads; capacities of 64 and 1024 exact usable
slots; and requested group sizes of 1, 4, and 16. Each command uses 6,000,000 completed measured
messages, 120,000 warmup messages, and three trials.

For every payload, capacity, and requested group size, four commands run in ABBA order: `bulk`,
`burst`, `burst`, `bulk`. Individual CSV rows remain preserved. Median completed messages per second
is the primary summary.

## Compared variants

- `bulk` retries until the complete requested group can be transferred, then publishes one counter
  advance;
- `burst` transfers the currently possible prefix, records the returned completed count, and keeps
  requesting the remaining suffix until the same complete group has been transferred.

Both modes use the same `BulkBurstBoundedRing`, inline storage, exact capacity, scalar counter
layout, payload generation and validation, yield waiting behavior, requested groups, final message
count, checksum work, and acquire/release ordering. The comparison changes only the progress
contract used by the workload.

## Results

No controlled measurements have been collected. Local and CI smoke commands validate only command
routing, completion, checksum behavior, and CSV fields. Their timings are not performance evidence.

## Interpretation

No performance conclusion is available before the controlled Linux run. A result must report
completed handoffs, not burst requests, and must distinguish partial progress from a successful
fixed-count bulk.

## Limitations

The steady-state workload does not deliberately impose producer imbalance or consumer stalls, so
partial burst frequency may vary incidentally. The comparison does not implement DPDK compatibility,
multi-producer or multi-consumer synchronization, staged reservation, direct storage access, cached
indices, alternate waiting strategies, or cross-NUMA placement.

## Reproduction

After verifying CPU topology and effective affinity, run:

```sh
cmake --preset release
cmake --build --preset release --parallel 4
lscpu -e=CPU,NODE,CORE,ONLINE
mkdir -p results/bulk-burst-comparison
for payload in 8 64 256; do
  for capacity in 64 1024; do
    for batch_size in 1 4 16; do
      run=0
      for implementation in bulk burst burst bulk; do
        run=$((run + 1))
        taskset -c 0 ./build/release/apps/handoff-bench/handoff-bench run throughput \
          --implementation "$implementation" \
          --payload-bytes "$payload" --capacity "$capacity" \
          --batch-size "$batch_size" \
          --iterations 6000000 --warmup 120000 --trials 3 \
          --producer-cpu 1 --consumer-cpu 2 \
          --output \
          "results/bulk-burst-comparison/${payload}b-${capacity}s-b${batch_size}-${run}-${implementation}.csv"
      done
    done
  done
done
```

Record the clean revision, host and toolchain metadata, CPU topology, tuning, and effective
placement. Preserve raw trials and separate observations from explanations.
