# Experiment: How does explicit sequence publication compare with head/tail SPSC?

- Type: implementation comparison
- Status: complete
- Plumbing revision: `a6fb5b0511083e0572a768915b9f5950c0203f56`
- Measurement revision: `48608c7471a2827e277a111bda07774be559b179`
- Date: 2026-09-15

## Question

Under equivalent scalar SPSC throughput and ping-pong workloads, how does the explicit
claim/populate/publish and observe/release lifecycle compare with the basic head/tail ring?

## Hypothesis

Both mechanisms coordinate one producer and one consumer with two acquire/release cursors and exact
bounded backpressure, so their coordination work may be similar. The sequence mechanism also
returns move-only tokens, keeps explicit side-local next and active state, uses one-based finite
sequences, exposes const observations, and accesses slots directly in throughput. Compiler
decisions, topology, and noise may dominate any difference.

## Setup

The controlled comparison ran on the physical four-core Intel N150 host under Ubuntu 26.04.1 with
Clang 21.1.8 and a fresh native Release build. The exact clean measurement revision passed all five
required CI jobs, then passed fresh Linux Release and clang-tidy builds and all 120 tests in each
configuration. The process was restricted to CPU 0; producer and consumer placement was applied
and verified on physical CPUs 1 and 2 in the same NUMA node. The stock `intel_pstate` `powersave`
governor, `balance_performance` EPP, boost state, and perf policy 4 were unchanged.

The canonical configuration used a 64-byte payload and 1024 exact usable slots. A 200,000,000-
message basic-throughput command conditioned the package first. Throughput used 20,000,000
measured and 2,000,000 warmup messages; ping-pong used 4,000,000 measured and 400,000 warmup
exchanges. Each workload used three one-trial ABBA blocks in the order `basic`, `sequence`,
`sequence`, `basic`, retaining six rows per implementation. The shortest timed row across the L1C
group was 2.222 seconds. All checksums matched, every requested affinity was applied and verified,
every benchmark stderr was empty, and thermal-throttle counters did not change.

## Compared implementations

- `basic` owns monotonic zero-based head/tail positions and transfers values through `try_push` and
  `try_pop` assignment;
- `sequence` owns one-based next-to-claim/observe state, publishes an atomic producer cursor, gates
  reuse with an atomic consumer sequence, and exposes explicit claim and observation tokens.

Both use default-constructed fixed slots, exact capacity, one producer, one consumer, lossless
backpressure, modulo slot reuse, yield waiting, conservative C++ acquire/release ordering, the same
payload generation and validation functions, completed operation counts, blocked phase control,
and output schema. This is an implementation comparison rather than a one-variable mechanism
isolation: token lifecycle, local state, direct throughput slot access, and consumer value-transfer
shape differ.

## Results

The primary summaries pool all six retained rows per implementation. Throughput deltas are positive
when `sequence` is faster; RTT deltas are positive when `sequence` is slower.

| Workload | Basic median | Sequence median | Delta | Basic sample CV | Sequence sample CV |
| --- | ---: | ---: | ---: | ---: | ---: |
| Throughput | 8,599,385 msg/s | 8,396,489 msg/s | -2.359% | 5.129% | 1.217% |
| Ping-pong median RTT | 788.0 ns | 813.0 ns | +3.173% | 3.780% | 2.878% |

Throughput full ranges were 10.162% and 3.228% of the respective medians. Its paired block deltas
were -8.335%, -2.359%, and +3.502%; the basic rows declined across the group and the paired
direction reversed in the final block. The throughput comparison is therefore inconclusive.

Ping-pong full ranges were 10.025% and 6.150%. Its paired RTT deltas were +0.370%, +2.064%, and
+3.560%, so all three blocks placed sequence RTT slightly higher, but command position materially
affected individual rows. Median p95 RTT was 810.0 ns for basic and 840.0 ns for sequence; median
p99 RTT was 825.0 ns and 851.5 ns. Raw CSV, stdout/stderr, sidecars, temperature/frequency
observations, throttle counters, and analysis remain in ignored `results/l1/l1c-sequence/` on both
hosts.

## Interpretation

The evidence does not rank the complete implementations for saturated throughput: pooled sequence
throughput was 2.359% lower, but dispersion exceeded that difference and paired direction changed.
There is no concrete hypothesis under which another identical block would resolve the drift, so no
fourth block was added.

Sequence ping-pong had a small directionally repeated RTT cost in this run. Its +3.173% pooled
median difference is close to the observed dispersion and sensitive to ABBA position, so it is a
conditional direction rather than a precise stable penalty. Neither result isolates sequence
numbering, token lifecycle, direct slot access, assignment shape, or any individual atomic.

## Limitations

The experiment covers one CPU, compiler, same-node core pair, payload size, capacity, waiting
strategy, and warm-state workload. It does not cover multiple producers, fan-out, dependency
gating, barriers, per-slot publication, batch claims, cross-NUMA placement, or realistic sequence
exhaustion. Ping-pong measures round-trip time rather than exact one-way latency. No PMU data was
collected, and the result establishes no compatibility with the LMAX API or Java memory model.

## Reproduction

From a clean detached checkout of the measurement revision after fresh Release and tidy gates:

```sh
bench=./build/release/apps/handoff-bench/handoff-bench
result_dir=results/l1/l1c-sequence
mkdir -p "$result_dir"

taskset -c 0 "$bench" run throughput \
  --implementation basic --payload-bytes 64 --capacity 1024 \
  --iterations 200000000 --warmup 2000000 --trials 1 \
  --producer-cpu 1 --consumer-cpu 2 --output "$result_dir/precondition.csv"

for workload in throughput ping-pong; do
  if [ "$workload" = throughput ]; then
    iterations=20000000
    warmup=2000000
  else
    iterations=4000000
    warmup=400000
  fi
  for block in 1 2 3; do
    position=0
    for implementation in basic sequence sequence basic; do
      position=$((position + 1))
      taskset -c 0 "$bench" run "$workload" \
        --implementation "$implementation" --payload-bytes 64 --capacity 1024 \
        --iterations "$iterations" --warmup "$warmup" --trials 1 \
        --producer-cpu 1 --consumer-cpu 2 \
        --output \
        "$result_dir/scalar-${workload}-block${block}-${position}-${implementation}.csv"
    done
  done
done
```

The retained sidecar records the exact host state, load, stock policy, placement,
temperature/frequency observations, and throttle-counter delta.
