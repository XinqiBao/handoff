# Experiment: Do cached remote indices change bounded SPSC performance?

- Type: mechanism isolation
- Status: planned
- Implementation revision: this record's introducing commit
- Measurement revision: to be recorded before the controlled Linux run
- Date: 2026-09-13

## Question

Does retaining producer-local and consumer-local copies of the last acquired remote index change
steady-state throughput or minimum-ish ping-pong RTT under otherwise equivalent bounded SPSC
workloads?

## Hypothesis

Remote-index caching may improve throughput when a thread can complete multiple operations before
its cached bound is exhausted, because most successful operations then avoid a shared atomic load.
Ping-pong alternates direction on every exchange and may offer less opportunity. Payload size,
capacity, cache hierarchy, topology, and noise may change the result; neither variant is expected
to be a universal winner.

## Setup

The planned Linux comparison uses the Release preset, two explicitly selected physical cores in
one NUMA node, payloads of 8, 64, and 256 bytes, and exact usable capacities of 64 and 1024 slots.
Throughput uses 5,000,000 measured messages; ping-pong uses 500,000 measured exchanges. Both use
100,000 warmup operations.

For each workload, payload, and capacity, four three-trial commands run in ABBA order: `basic`,
`cached-index`, `cached-index`, `basic`. Individual trial rows remain in separate CSV files. The
primary summaries are median messages per second for throughput and median of per-trial median RTTs
for ping-pong. P95 and p99 RTT remain supporting observations.

## Compared variants

The intended independent variable is remote-index read frequency:

- `basic` acquires the remote index on every push and pop attempt;
- `cached-index` acquires the remote head only when its cached head makes the ring appear full and
  acquires the remote tail only when its cached tail makes the ring appear empty.

Both variants use the same default-constructed inline slots, compile-time exact usable capacity,
monotonic counters, modulo addressing, assignment-based payload lifetime, non-blocking
`try_push`/`try_pop` API, and relaxed-own/acquire-remote/release-publish ordering. The benchmark
instantiates the same workload templates, so payload generation, validation, waiting, phase
control, timing, and output are unchanged.

## Results

No controlled measurements have been collected. A pre-commit local plumbing matrix covered both
workloads, all three supported payload sizes, both capacities, and `basic`/`cached-index`, for 24
commands total. Every command completed internal validation, every CSV data row had 14 fields, and
the two implementations produced matching checksums for each workload/payload/capacity tuple. The
ignored local timings are not experimental evidence.

## Interpretation

No performance conclusion is available before a controlled Linux run with verified effective
affinity and preserved raw trials.

## Limitations

The cached variant necessarily adds two non-atomic counter-sized members. Object size, member
placement, compiler code generation, and cache-set mapping therefore remain structural confounders
alongside the intended change in remote-index load frequency. The experiment does not isolate
coherence traffic without external counters and does not cover batching, offered-load latency,
other waiting strategies, cross-NUMA placement, or topologies beyond one producer and one consumer.

Frequency scaling, thermal state, background work, command order, and timer overhead remain sources
of variation. Results near the measured noise floor must be reported as inconclusive.

## Reproduction

Build and verify the exact measurement revision before running. The planned controlled commands,
conditional on confirming the chosen CPUs are online, on distinct physical cores, and in one NUMA
node, are:

```sh
cmake --preset release
cmake --build --preset release --parallel 4
lscpu -e=CPU,NODE,CORE,ONLINE
mkdir -p results/cached-index-comparison
for workload in throughput ping-pong; do
  case "$workload" in
    throughput) iterations=5000000 ;;
    ping-pong) iterations=500000 ;;
  esac
  for payload in 8 64 256; do
    for capacity in 64 1024; do
      run=0
      for implementation in basic cached-index cached-index basic; do
        run=$((run + 1))
        ./build/release/apps/handoff-bench/handoff-bench run "$workload" \
          --implementation "$implementation" \
          --payload-bytes "$payload" --capacity "$capacity" \
          --iterations "$iterations" --warmup 100000 --trials 3 \
          --producer-cpu 2 --consumer-cpu 4 \
          --output \
          "results/cached-index-comparison/${workload}-${payload}b-${capacity}s-${run}-${implementation}.csv"
      done
    done
  done
done
```

Before execution, record the clean revision, toolchain and host metadata, CPU topology, system
tuning, and exact effective affinity. Retain all trial rows and report observations separately from
causal explanations.
