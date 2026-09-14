# Experiment: Cost of a fixed sequence dependency

- Type: mechanism isolation
- Status: planned
- Revision:
- Date:

## Question

How does requiring a publication to pass through two ordered consumer stages change completed
throughput relative to two independent reliable consumers?

## Hypothesis

The pipeline replaces independent availability with a producer-to-upstream-to-downstream
happens-before chain while retaining two validations and downstream-gated reuse. Its effect may
depend on payload size, capacity, stage placement, and scheduling; smoke timings cannot determine
the magnitude or cause.

## Setup

Follow the repository benchmark methodology on a controlled Linux host. Use the same compiler,
build, payload generation, capacity, waiting behavior, warmup, iteration count, and trial count for
both variants. Use coordinator CPU 0, producer CPU 1, and consumer CPUs 2 and 3 on the verified
single-node host. List order maps to fan-out consumer 0/1 and pipeline upstream/downstream.

## Compared variants

- `fan-out`: two independent reliable consumers observe each producer publication, with reuse gated
  by their minimum release sequence;
- `pipeline`: the downstream consumer observes only after upstream release, with reuse gated by
  downstream release.

Both variants perform two validations and count a publication only after both consumers release it.
The intended difference is independent fan-out versus fixed dependency ordering.

## Results

No controlled measurements have been recorded.

## Interpretation

No performance interpretation is available before controlled results exist.

## Limitations

The workloads model only two fixed consumer roles and yield-based waiting. They do not represent an
arbitrary dependency graph or different work at successive application stages. CPU topology,
scheduler interference, state placement, and a slow stage may dominate the result.

## Reproduction

Use the Release preset and preserve all trial rows. The workload shapes are:

```sh
taskset -c 0 ./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation fan-out --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5 \
  --producer-cpu 1 --consumer-cpus 2,3 --output fan-out.csv
taskset -c 0 ./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation pipeline --payload-bytes 64 --capacity 1024 \
  --iterations 1000000 --warmup 10000 --trials 5 \
  --producer-cpu 1 --consumer-cpus 2,3 --output pipeline.csv
```
