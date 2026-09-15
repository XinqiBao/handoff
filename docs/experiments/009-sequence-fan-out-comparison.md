# Experiment: Cost of reliable sequence fan-out

- Type: implementation comparison
- Status: complete
- Plumbing revision: `d12f5e757979a803cfd4e04a4a10df9d4686f4e2`
- Measurement revision: `48608c7471a2827e277a111bda07774be559b179`
- Date: 2026-09-15

## Question

How does requiring every publication to be observed and released by two independent consumers
change completed-publication throughput relative to the one-consumer sequence baseline?

## Hypothesis

The fan-out contract will do more work per publication because it requires a second reliable
delivery and validation, an additional worker, a second gating sequence, and a producer minimum-
gating scan. The comparison can characterize the complete contract but cannot isolate those costs.

## Setup

This comparison shared experiment 008's clean CI-green measurement revision, fresh Linux Release
and tidy gates, physical Intel N150 host, 64-byte payload, 1024 exact usable slots, stock HWP
policy, and common warm-state conditioning. Both routes used 20,000,000 measured publications and
2,000,000 warmup publications. The process coordinator was restricted to CPU 0 and the producer
was verified on CPU 1. `sequence` used one verified consumer on CPU 2; `fan-out` used consumer 0 on
CPU 2 and consumer 1 on CPU 3, with every effective mask recorded.

Three one-trial ABBA blocks ran in the order `sequence`, `fan-out`, `fan-out`, `sequence`, retaining
six rows per implementation. All consumer checksums matched the same expected value, every
benchmark stderr was empty, the shortest timed row in this comparison was 2.351 seconds, perf
policy remained 4, and thermal-throttle counters did not change.

## Compared contracts

- `sequence`: one producer and one reliable consumer using contiguous sequence publication;
- `fan-out`: one producer and two independent reliable consumers, with reuse gated by their
  minimum released sequence.

Both count completed publications rather than aggregate deliveries. `fan-out` completion includes
two required observations, validations, and releases; `sequence` includes one. The delivery work,
worker count, gating state, and topology are inseparable parts of this richer-contract comparison.

## Results

| Sequence median | Fan-out median | Delta | Sequence sample CV | Fan-out sample CV |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 8,427,636 msg/s | 4,770,968 msg/s | -43.389% | 1.575% | 4.293% |

The full ranges were 4.216% and 11.782% of the respective medians. Paired block deltas were
-44.786%, -42.691%, and -39.732%, so fan-out completed fewer publications per second in all three
blocks. Fan-out also rose across the group; its first-to-last change was +10.231%, which makes the
exact magnitude less stable than the repeated direction. Raw evidence remains in ignored
`results/l1/l1c-sequence/` on both hosts.

## Interpretation

On this host and workload, requiring two reliable deliveries had a clear complete-contract
throughput cost relative to the single-consumer sequence route. The observed median difference was
-43.389%, and every paired block agreed despite drift in the fan-out rows.

The result must not be read as the cost of one atomic operation, one additional consumer in
isolation, a minimum scan in isolation, or sequence publication itself. Fan-out performs a second
payload validation and release, runs an additional worker, and adds slowest-reader gating. It buys
reliable independent observation of every publication by both consumers; the sequence baseline
delivers each publication once.

## Limitations

CPU 3 has greater historical network softirq activity than the preferred two-worker CPUs, so the
three-worker route cannot receive topology-equivalent placement on this four-core host. The
experiment covers one consumer count, CPU, compiler, payload, capacity, placement, waiting
strategy, and saturated workload. It does not model slow readers, dynamic consumers, work sharing,
lossy broadcast, arbitrary placement, or cross-NUMA execution. No PMU data was collected, and this
is not an LMAX compatibility result.

## Reproduction

After the build, gates, and conditioning command shown in experiment 008:

```sh
bench=./build/release/apps/handoff-bench/handoff-bench
result_dir=results/l1/l1c-sequence

run_fan_out_pair() {
  block=$1
  position=0
  for implementation in sequence fan-out fan-out sequence; do
    position=$((position + 1))
    if [ "$implementation" = fan-out ]; then
      taskset -c 0 "$bench" run throughput \
        --implementation "$implementation" --payload-bytes 64 --capacity 1024 \
        --iterations 20000000 --warmup 2000000 --trials 1 \
        --producer-cpu 1 --consumer-cpus 2,3 \
        --output "$result_dir/fan-out-throughput-block${block}-${position}-${implementation}.csv"
    else
      taskset -c 0 "$bench" run throughput \
        --implementation "$implementation" --payload-bytes 64 --capacity 1024 \
        --iterations 20000000 --warmup 2000000 --trials 1 \
        --producer-cpu 1 --consumer-cpu 2 \
        --output "$result_dir/fan-out-throughput-block${block}-${position}-${implementation}.csv"
    fi
  done
}
for block in 1 2 3; do run_fan_out_pair "$block"; done
```

The retained sidecar records exact provenance, verified role placement, host load, stock policy,
temperature/frequency observations, and throttle-counter delta.
