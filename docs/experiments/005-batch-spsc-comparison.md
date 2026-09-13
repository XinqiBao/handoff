# Experiment: Does all-or-nothing batch publication change SPSC throughput?

- Type: mechanism isolation
- Status: planned
- Implementation revision: this record's introducing commit
- Measurement revision: to be recorded before the controlled Linux run
- Date: 2026-09-13

## Question

How does publishing fixed groups of messages with one counter update change steady-state SPSC
throughput relative to publishing the same messages individually?

## Hypothesis

Batch publication may reduce shared counter and ordering overhead per message, with larger effects
for small payloads. It may also increase waiting granularity because a complete group must fit or be
available before progress occurs. The balance may depend on batch size, capacity, payload size,
topology, and system noise.

## Setup

The planned controlled Linux comparison uses a clean Release build and two verified physical cores
in one NUMA node. It covers 8, 64, and 256 byte payloads; capacities of 64 and 1024 exact usable
slots; and batch sizes 1, 4, and 16. Each command uses 6,000,000 measured messages, 120,000 warmup
messages, and three trials.

For each payload, capacity, and batch size, four commands run in ABBA order: `basic`, `batch`,
`batch`, `basic`. Individual CSV rows remain preserved. Median completed messages per second is the
primary summary.

## Compared variants

- `basic` generates each fixed group but calls scalar push and pop for every message, publishing
  each slot separately;
- `batch` generates the same group and uses one all-or-nothing push and pop, publishing the complete
  group once.

Both variants use identical inline slot storage, exact capacity, payload generation and validation,
yield waiting behavior, total message count, checksum work, phase control, and conservative
acquire/release ordering. A batch size of one checks that workload routing does not change the
logical work.

## Results

No controlled measurements have been collected. A pre-commit local plumbing matrix covered all
three payload sizes, both capacities, all three batch sizes, and `basic`/`batch`, for 36 commands
total. Every command completed internal validation, CSV recorded the requested batch size in its
14-field data row, and both implementations produced matching checksums for each
payload/capacity/batch tuple. The ignored local timings are not performance evidence.

## Interpretation

No performance conclusion is available before the controlled Linux run. Any result must account
for both reduced publication frequency and different all-or-nothing progress granularity.

## Limitations

This comparison does not isolate individual causes within the batch operation, measure best-effort
burst semantics, or cover ping-pong, offered-load latency, other waiting strategies, cross-NUMA
placement, or multiple producers or consumers. Compiler unrolling and code-size differences are
possible confounders. Results near the noise floor must be reported as inconclusive.

## Reproduction

After verifying CPU topology and effective affinity, run:

```sh
cmake --preset release
cmake --build --preset release --parallel 4
lscpu -e=CPU,NODE,CORE,ONLINE
mkdir -p results/batch-comparison
for payload in 8 64 256; do
  for capacity in 64 1024; do
    for batch_size in 1 4 16; do
      run=0
      for implementation in basic batch batch basic; do
        run=$((run + 1))
        ./build/release/apps/handoff-bench/handoff-bench run throughput \
          --implementation "$implementation" \
          --payload-bytes "$payload" --capacity "$capacity" \
          --batch-size "$batch_size" \
          --iterations 6000000 --warmup 120000 --trials 3 \
          --producer-cpu 2 --consumer-cpu 4 \
          --output \
          "results/batch-comparison/${payload}b-${capacity}s-b${batch_size}-${run}-${implementation}.csv"
      done
    done
  done
done
```

Record the clean revision, host and toolchain metadata, CPU topology, tuning, and effective
placement. Preserve raw trials and separate observations from explanations.
