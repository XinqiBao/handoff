# Experiment: Cost of a fixed sequence dependency

- Type: mechanism isolation
- Status: complete
- Plumbing revision: `bff701def33601363ae3d22db86959aea9e3970c`
- Measurement revision: `48608c7471a2827e277a111bda07774be559b179`
- Date: 2026-09-15

## Question

How does requiring a publication to pass through two ordered consumer stages change completed
throughput relative to two independent reliable consumers?

## Hypothesis

The pipeline replaces independent availability and minimum gating with a producer-to-upstream-to-
downstream happens-before chain while retaining two validations and downstream-gated reuse. The
changed dependency may affect completed throughput, but host noise and placement may dominate a
small difference.

## Setup

This comparison shared experiment 008's clean CI-green measurement revision, fresh Linux Release
and tidy gates, physical Intel N150 host, 64-byte payload, 1024 exact usable slots, stock HWP
policy, common warm-state conditioning, and 20,000,000 measured plus 2,000,000 warmup publications.
The process was restricted to CPU 0 and the producer was verified on CPU 1. Fan-out consumer 0 and
pipeline upstream were verified on CPU 2; fan-out consumer 1 and pipeline downstream were verified
on CPU 3.

Three one-trial ABBA blocks ran in the order `fan-out`, `pipeline`, `pipeline`, `fan-out`, retaining
six rows per implementation. Both validations produced the expected checksum on every row, every
benchmark stderr was empty, the shortest timed row was 3.855 seconds, perf policy remained 4, and
thermal-throttle counters did not change.

## Compared variants

- `fan-out`: two independent reliable consumers observe each producer publication, with reuse gated
  by their minimum release sequence;
- `pipeline`: downstream observation requires upstream release, with reuse gated by downstream
  release.

Both variants use one producer, two consumers, two complete payload validations, yield waiting,
the same role placement, and downstream-complete publication counts. The changed mechanism is
independent observation with minimum gating versus a fixed ordered dependency and downstream
gating.

## Results

| Fan-out median | Pipeline median | Delta | Fan-out sample CV | Pipeline sample CV |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 4,909,678 msg/s | 4,909,227 msg/s | -0.009% | 4.059% | 4.125% |

The full ranges were 9.017% and 10.030% of the respective medians. Paired block deltas for pipeline
were +2.914%, -6.338%, and +4.932%. The pooled medians were nearly identical, but block direction
alternated and dispersion was several orders of magnitude larger than the median difference. Working
output was collected under ignored `results/l1/l1c-sequence/`.

## Interpretation

The comparison is inconclusive. Under this canonical workload, the evidence does not rank
independent fan-out and fixed dependency ordering. Their nearly equal pooled medians do not prove
that the mechanisms have equal cost, because both distributions are broad and paired direction
changes.

The experiment does establish that the two complete routes can sustain the same approximate
throughput regime while providing different semantics: fan-out permits independent observation and
gates reuse on the slowest consumer, whereas pipeline enforces upstream completion before
downstream observation. No additional identical block is justified without a hypothesis for the
observed variability.

## Limitations

CPU 3's greater historical network softirq activity remains a four-core-host limitation, although
both variants use the same placement. The fixed validation work is intentionally identical at both
roles and does not represent different application stages. The experiment covers one CPU,
compiler, payload, capacity, placement, waiting strategy, and saturated workload. It does not
cover a slow stage, arbitrary dependency graphs, dynamic consumers, cross-NUMA placement, or other
consumer counts. No PMU data was collected.

## Reproduction

After the build, gates, and conditioning command shown in experiment 008:

```sh
bench=./build/release/apps/handoff-bench/handoff-bench
result_dir=results/l1/l1c-sequence

for block in 1 2 3; do
  position=0
  for implementation in fan-out pipeline pipeline fan-out; do
    position=$((position + 1))
    taskset -c 0 "$bench" run throughput \
      --implementation "$implementation" --payload-bytes 64 --capacity 1024 \
      --iterations 20000000 --warmup 2000000 --trials 1 \
      --producer-cpu 1 --consumer-cpus 2,3 \
      --output \
      "$result_dir/dependency-throughput-block${block}-${position}-${implementation}.csv"
  done
done
```

The retained sidecar records exact provenance, verified role placement, host load, stock policy,
temperature/frequency observations, and throttle-counter delta.
