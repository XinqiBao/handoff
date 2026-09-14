# Experiment: What changes when SP/SC slot access is staged?

- Type: mechanism isolation
- Status: planned
- Implementation revision: this record's introducing commit
- Measurement revision: to be recorded before the controlled Linux run
- Date: 2026-09-13

## Question

For equal fixed message groups, how does reserving ring slots and accessing them directly before one
finish operation differ from an all-or-nothing bulk operation that transfers through an intermediate
payload array?

## Hypothesis

Staged access may reduce intermediate assignment work and gives the caller explicit ownership of
physical ring spans. Its move-only token lifecycle and span traversal may introduce other costs.
The balance may depend on payload size, requested group size, physical wrap, compiler decisions, and
system noise.

## Setup

The planned controlled Linux comparison uses a clean Release build and two verified physical cores
in one NUMA node. It covers 8, 64, and 256 byte payloads; capacities of 64 and 1024 exact usable
slots; and requested group sizes of 1, 4, and 16. Each command uses 6,000,000 completed measured
messages, 120,000 warmup messages, and three trials.

For each payload, capacity, and group size, four commands run in ABBA order: `bulk`, `staged`,
`staged`, `bulk`. Individual CSV rows remain preserved. Median completed messages per second is the
primary summary.

## Compared variants

- `bulk` generates a group in a local array, copy-assigns it into ring slots, move-assigns it back
  into a consumer array, and publishes or releases once per complete group;
- `staged` reserves the same complete group, generates payloads into its writable ring spans,
  validates through its const consumer spans, and explicitly finishes once on each side.

Both use default-constructed fixed slots, exact capacity, one producer and one consumer, the same
payload generation and observation functions, requested groups, final message count, checksum,
yield waiting behavior, and conservative acquire/release ordering. The staged mode's direct slot
access is the isolated difference; it is not an end-to-end no-copy claim.

## Results

No controlled measurements have been collected. Local and CI smoke commands validate only command
routing, completion, checksum behavior, and CSV fields. Their timings are not performance evidence.

## Interpretation

No performance conclusion is available before the controlled Linux run. Results must describe the
specific removed intermediate assignments and explicit reservation lifecycle rather than attribute
all differences to a vague copy count.

## Limitations

Configured capacities and group sizes do not intentionally make every benchmark group cross the
physical wrap boundary; deterministic and concurrent correctness tests cover that path. The
comparison does not include DPDK compatibility, staged partial finish, cancellation frequency,
multi-producer or multi-consumer synchronization, alternate waiting strategies, or cross-NUMA
placement.

## Reproduction

After verifying CPU topology and effective affinity, run:

```sh
cmake --preset release
cmake --build --preset release --parallel 4
lscpu -e=CPU,NODE,CORE,ONLINE
mkdir -p results/staged-comparison
for payload in 8 64 256; do
  for capacity in 64 1024; do
    for batch_size in 1 4 16; do
      run=0
      for implementation in bulk staged staged bulk; do
        run=$((run + 1))
        taskset -c 0 ./build/release/apps/handoff-bench/handoff-bench run throughput \
          --implementation "$implementation" \
          --payload-bytes "$payload" --capacity "$capacity" \
          --batch-size "$batch_size" \
          --iterations 6000000 --warmup 120000 --trials 3 \
          --producer-cpu 1 --consumer-cpu 2 \
          --output \
          "results/staged-comparison/${payload}b-${capacity}s-b${batch_size}-${run}-${implementation}.csv"
      done
    done
  done
done
```

Record the clean revision, host and toolchain metadata, CPU topology, tuning, and effective
placement. Preserve raw trials and separate observations from explanations.
