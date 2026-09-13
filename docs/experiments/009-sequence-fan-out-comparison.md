# Experiment: Cost of reliable sequence fan-out

- Type: mechanism isolation
- Status: planned
- Revision:
- Date:

## Question

How does requiring every publication to be observed and released by two independent consumers
change completed-publication throughput relative to the one-consumer sequence baseline?

## Hypothesis

The fan-out mechanism will do more coordination per publication because the producer scans two
gating sequences and completion waits for both consumers. The magnitude and sensitivity to payload
size and capacity require controlled Linux evidence; smoke timings cannot answer the question.

## Setup

Follow the repository benchmark methodology on a controlled Linux host. Use the same compiler,
build, payload generation, capacity, waiting behavior, warmup, iteration count, and trial count for
both variants. Before running the comparison, extend placement recording so the producer and both
fan-out consumers have explicit, verified CPUs on the intended NUMA node.

## Compared variants

- `sequence`: one producer and one reliable consumer using contiguous sequence publication;
- `fan-out`: one producer and two reliable consumers, with reuse gated by their minimum released
  sequence.

The delivery topology and gating count are the intended differences. `iterations` counts
publications fully observed and released by all consumers, not aggregate deliveries.

## Results

No controlled measurements have been recorded.

## Interpretation

No performance interpretation is available before controlled results exist.

## Limitations

The variants have different required consumer work by definition: one observation versus two. The
comparison characterizes the cost of the reliable fan-out contract, not an isolated atomic
instruction. CPU topology, slow-reader behavior, cache placement, and scheduler interference may
all affect results.

## Reproduction

After multi-consumer CPU placement is implemented and verified, use the Release preset and preserve
all trial rows. The workload shapes are:

```sh
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation sequence --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5 --output sequence.csv
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation fan-out --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5 --output fan-out.csv
```

Do not treat these unpinned command forms as the final controlled protocol.
