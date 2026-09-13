# Experiment: Record descriptor and payload separation

- Type: implementation comparison
- Status: planned
- Revision:
- Date:

## Question

How does separating fixed descriptors from variable payload bytes affect throughput and ping-pong
RTT relative to fixed inline records and contiguous `[header][payload]` byte-ring records?

## Hypothesis

The split layout removes headers and padding markers from the payload ring but adds a descriptor
lookup and two independently constrained resources. Payload size and wrap frequency may change
which cost or capacity becomes important. Smoke timings cannot establish these effects.

## Setup

Follow the repository benchmark methodology on a controlled Linux host. Use the same compiler,
build, CPUs, NUMA placement, waiting behavior, logical payload sizes, warmup, iterations, and trial
count. Verify requested and effective affinity and preserve every trial row.

Record native capacity dimensions without converting them: `fixed-record` uses 64 or 1024 slots,
`byte-record` uses 4096 or 65536 bytes, and `descriptor-record` uses the paired 64/4096 or
1024/65536 descriptor/byte capacities. For each payload size, report fixed-record object size,
byte-record aligned `[header][payload]` footprint, descriptor-record aligned payload footprint, and
the expected wrap behavior beside raw results.

## Compared variants

- `fixed-record`: one complete fixed-capacity record object per exact-capacity slot;
- `byte-record`: logical headers and payloads share a byte ring with explicit padding headers;
- `descriptor-record`: fixed descriptors reference payloads in a separate byte ring whose end gaps
  are accounted by the owning descriptor.

All variants use scalar copy-in/copy-out operations, lossless SPSC backpressure, the shared logical
header, the same payload generation and validation, conservative acquire/release ordering, yield
waiting, and equivalent phase boundaries. This is an implementation comparison, not a one-variable
mechanism-isolation experiment.

## Results

No controlled measurements have been recorded.

## Interpretation

No performance interpretation is available before controlled results exist.

## Limitations

The benchmark uses a fixed logical payload length per run even though both variable layouts accept
mixed lengths. Descriptor slots and payload bytes can become limiting independently. The supported
capacity pairs reduce the experiment matrix but do not equalize resident record count, object
footprint, alignment loss, or backpressure frequency across implementations.

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
./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation descriptor-record --payload-bytes 64 \
  --capacity 64 --capacity-bytes 4096 \
  --iterations 1000000 --warmup 10000 --trials 5 --output descriptor-record.csv
```

Do not treat these unpinned command forms as the final controlled protocol.
