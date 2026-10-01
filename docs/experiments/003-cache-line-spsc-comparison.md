# Experiment: Does counter separation change bounded SPSC performance?

- Type: mechanism isolation
- Status: complete
- Plumbing revision: `b63246e2d0e2fe6a61dd4a0b90774e329a01d987`
- Measurement revision: `ffe279ffaf06366ec0f77f4c5c27782864f523bc`
- Date: 2026-09-14

## Question

Does placing the consumer-owned `head` and producer-owned `tail` counters in separate aligned state
blocks change steady-state throughput or minimum-ish ping-pong RTT under otherwise equivalent
bounded SPSC workloads?

## Hypothesis

Counter separation may reduce counter-line interference when producer and consumer run on different
cores. Ping-pong is dependency-bound and may show a smaller or different effect. Object size,
alignment, cache hierarchy, topology, and noise may all affect the result; neither implementation is
expected to be a universal winner.

## Setup

The controlled comparison ran on the physical four-core Intel N150 host under Ubuntu 26.04.1 with
Clang 21.1.8 and a fresh native Release build. The exact measurement revision passed the Linux
Release, format, clang-tidy, ASan/UBSan, and TSan gates. The process was restricted to CPU 0; the
producer and consumer reported verified effective placement on physical CPUs 1 and 2 in the same
NUMA node. The stock `intel_pstate` `powersave` governor, `balance_performance` EPP, boost state,
and perf policy 4 were unchanged.

The canonical configuration used a 64-byte payload and 1024 exact usable slots. A 200,000,000-
message basic-throughput command conditioned the package first. Throughput commands used 20,000,000
measured messages and 2,000,000 warmup messages. Ping-pong commands used 3,000,000 measured
exchanges and 300,000 warmup exchanges. Each workload used three one-trial ABBA blocks in the order
`basic`, `cache-line`, `cache-line`, `basic`, retaining six rows per implementation. The shortest
timed row was 2.137 seconds. All checksums matched within each workload, every requested affinity
was applied and verified, and thermal-throttle counters did not change.

## Compared variants

- `basic`: `head` and `tail` are adjacent atomic members.
- `cache-line`: `head` and `tail` occupy separate 128-byte-aligned state blocks.

Both variants use the same inline slots, compile-time exact usable capacity, monotonic counters,
modulo addressing, scalar `try_push`/`try_pop` operations, remote-index read on every attempt, and
relaxed-own/acquire-remote/release-publish memory orders. The common workload keeps payload
generation, validation, mechanism-side `yield` waiting, blocked phase control, timing, and result
work unchanged.

## Results

The primary summaries pool all six retained rows for each implementation. Throughput deltas are
positive when `cache-line` is faster; ping-pong deltas are negative when its RTT is lower.

| Workload | Basic median | Cache-line median | Delta | Basic sample CV | Cache-line sample CV |
| --- | ---: | ---: | ---: | ---: | ---: |
| Throughput | 8,103,996 msg/s | 8,989,830 msg/s | +10.931% | 0.764% | 0.667% |
| Ping-pong median RTT | 812.0 ns | 747.5 ns | -7.943% | 3.104% | 0.649% |

The throughput full ranges were 2.012% and 1.752% of the respective medians. The ping-pong full
ranges were 7.020% and 1.472%. Paired block deltas were +11.888%, +10.524%, and +11.212% for
throughput and -4.765%, -10.233%, and -8.170% for ping-pong. The direction therefore held in every
block and was materially larger than the 0.280% warm-state basic-throughput CV established in L1A.
Working output was collected under ignored `results/l1/l1b-scalar/repetition/`; the observations
and conditions above are the durable record.

## Interpretation

For this N150, Clang build, canonical payload/capacity, placement, and saturated workload, the
complete cache-line-separated variant had a stable conditional advantage over the basic ring in
both completed-message throughput and minimum-ish ping-pong RTT. This supports retaining counter
separation as a useful structural variant under the measured conditions.

The result does not prove that reduced coherence traffic caused the difference. No PMU event that
identifies counter-line invalidation was collected, and the comparison includes the variant's
complete representation change.

## Limitations

The 128-byte state blocks increase object size and alignment. Object placement, inline storage
position, compiler code generation, and cache-set mapping remain structural confounders alongside
counter separation. The experiment covers one CPU, compiler, same-node core pair, payload size,
capacity, waiting strategy, and warm-state saturated workload. It does not establish cross-machine
behavior, cross-NUMA behavior, offered-load behavior, or a universal ranking. Ping-pong RTT is a
round trip and is not exact one-way latency.

## Reproduction

From a clean detached checkout of the measurement revision on the Linux host:

```sh
cmake --preset release --fresh
cmake --build --preset release --clean-first --parallel 4
ctest --preset release --no-tests=error

bench=./build/release/apps/handoff-bench/handoff-bench
result_dir=results/l1/l1b-scalar/repetition
mkdir -p "$result_dir"

taskset -c 0 "$bench" run throughput \
  --implementation basic --payload-bytes 64 --capacity 1024 \
  --iterations 200000000 --warmup 2000000 --trials 1 \
  --producer-cpu 1 --consumer-cpu 2 \
  --output "$result_dir/precondition-cache-line.csv"

for workload in throughput ping-pong; do
  if [ "$workload" = throughput ]; then
    iterations=20000000
    warmup=2000000
  else
    iterations=3000000
    warmup=300000
  fi
  for block in 1 2 3; do
    position=0
    for implementation in basic cache-line cache-line basic; do
      position=$((position + 1))
      taskset -c 0 "$bench" run "$workload" \
        --implementation "$implementation" --payload-bytes 64 --capacity 1024 \
        --iterations "$iterations" --warmup "$warmup" --trials 1 \
        --producer-cpu 1 --consumer-cpu 2 \
        --output \
        "$result_dir/cache-line-$workload-b$block-$position-$implementation.csv"
    done
  done
done
```

The retained sidecar records the host, exact clean SHA, topology, load, stock power policy,
temperature/frequency observations, and throttle-counter delta.

## Current applicability

This record describes its stated revision and conditions. The later correctness campaign restricts
wrapping fixed-slot SPSC families to power-of-two slot capacities so physical mapping remains valid
at machine-counter rollover. Ordinary physical wrap in these historical tests did not exercise that
rollover. Existing benchmark capacities remain supported, but these measurements are not renewed
evidence for the current implementation.

[Experiment 023](023-spsc-measurement-stability.md) and its supporting controls later exposed
workload-dependent process states and unresolved small scalar throughput gaps.
[Experiment 030](030-spsc-rtt-repeatability.md) supports only its conditional two-ring tail direction.
Those later limits prevent promoting this historical observation into a current general ranking.
