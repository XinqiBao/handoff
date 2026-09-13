# Experiment: Generic payload versus fixed record slots

- Type: mechanism isolation
- Status: planned
- Revision:
- Date:

## Question

How does copying and validating an explicit 16-byte record header plus an inline fixed-capacity
payload change throughput and ping-pong RTT relative to the generic fixed-slot SPSC baseline?

## Hypothesis

The fixed-record mode transfers more bytes and validates sequence, type, and logical length in
addition to the same payload work. The effect may vary with payload size and slot capacity, and its
magnitude requires controlled Linux evidence; smoke timings cannot answer the question.

## Setup

Follow the repository benchmark methodology on a controlled Linux host. Use the same compiler,
build, CPUs, NUMA placement, waiting behavior, logical payload sizes, slot capacities, warmup,
iterations, and trial count. Verify requested and effective affinity and retain all trial rows.

## Compared variants

- `basic`: a generic `Payload<N>` copied through the basic fixed-slot SPSC ring;
- `fixed-record`: the same `N` payload bytes plus a 16-byte fixed record header copied through the
  fixed-record SPSC ring.

Both use exact slot capacity, the same head/tail algorithm, acquire/release ordering, payload
generation, payload validation, checksums, and phase boundaries. Header transfer and validation are
part of the intended fixed-record contract, not overhead to subtract.

## Results

No controlled measurements have been recorded.

## Interpretation

No performance interpretation is available before controlled results exist.

## Limitations

This comparison does not isolate individual header fields or compare variable-sized storage. The
record duplicates the sequence already encoded in the benchmark payload. Compiler code generation,
object trailing padding, cache placement, and scheduler interference may influence results.

## Reproduction

Use the Release preset and preserve individual throughput and ping-pong rows for `basic` and
`fixed-record`. A representative pair of throughput command shapes is:

```sh
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation basic --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5 --output basic.csv
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation fixed-record --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5 --output fixed-record.csv
```

Do not treat these unpinned command forms as the final controlled protocol.
