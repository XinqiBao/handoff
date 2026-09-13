# Experiment: Fixed slots versus a variable-record byte ring

- Type: implementation comparison
- Status: planned
- Revision:
- Date:

## Question

How do contiguous variable-length byte-ring storage, aligned physical footprints, header parsing,
and explicit wrap padding affect throughput and ping-pong RTT relative to fixed record slots?

## Hypothesis

The byte ring may change cache occupancy and copying behavior, while payload sizes whose aligned
footprints do not divide the buffer also require padding records. The mechanisms expose different
native capacity dimensions, so controlled evidence and careful capacity accounting are required;
smoke timings cannot answer the question.

## Setup

Follow the repository benchmark methodology on a controlled Linux host. Use the same compiler,
build, CPUs, NUMA placement, waiting behavior, logical payload sizes, warmup, iterations, and trial
count. Verify requested and effective affinity and retain every trial row.

Record both native dimensions rather than converting one into a misleading label: `fixed-record`
uses 64 or 1024 slots, while `byte-record` uses 4096 or 65536 bytes. For each payload size, report
the fixed record object size, byte-ring aligned footprint, maximum resident record count before
padding, and expected wrap pattern beside the raw results.

## Compared variants

- `fixed-record`: one complete fixed-capacity record object per exact-capacity slot;
- `byte-record`: contiguous 16-byte-aligned `[header][payload]` footprints in one byte buffer, with
  explicit padding headers at physical wrap.

Both use the shared header fields, scalar copy-in/copy-out operations, lossless SPSC backpressure,
the same payload generation and validation, conservative acquire/release ordering, yield waiting,
and equivalent phase boundaries. This is not a one-variable comparison because storage capacity
and layout semantics differ.

## Results

No controlled measurements have been recorded.

## Interpretation

No performance interpretation is available before controlled results exist.

## Limitations

The benchmark uses one fixed logical payload length per run even though the byte ring supports mixed
lengths. Native slot and byte capacities are not interchangeable, and differing resident record
counts may affect backpressure. The comparison does not isolate parsing, alignment, padding, or
copying individually.

## Reproduction

Use the Release preset and preserve individual throughput and ping-pong rows. Representative
throughput command shapes are:

```sh
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation fixed-record --payload-bytes 64 --capacity 64 \
  --iterations 1000000 --warmup 10000 --trials 5 --output fixed-record.csv
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation byte-record --payload-bytes 64 --capacity-bytes 4096 \
  --iterations 1000000 --warmup 10000 --trials 5 --output byte-record.csv
```

Do not treat these unpinned command forms as the final controlled protocol.
