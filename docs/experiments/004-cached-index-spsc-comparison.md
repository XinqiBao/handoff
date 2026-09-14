# Experiment: Do cached remote indices change bounded SPSC performance?

- Type: mechanism isolation
- Status: complete
- Measurement revision: `ffe279ffaf06366ec0f77f4c5c27782864f523bc`
- Date: 2026-09-14

## Question

Does retaining producer-local and consumer-local copies of the last acquired remote index change
steady-state throughput or minimum-ish ping-pong RTT under otherwise equivalent bounded SPSC
workloads?

## Hypothesis

Remote-index caching may improve throughput when a thread can complete multiple operations before
its cached bound is exhausted, because most successful operations then avoid a shared atomic load.
Ping-pong alternates direction on every exchange and may offer less opportunity. Payload size,
capacity, cache hierarchy, topology, and noise may change the result; neither variant is expected
to be a universal winner.

## Setup

The comparison used the same CI-green measurement revision, fresh Linux Release build, physical
Intel N150 host, stock HWP policy, and verified coordinator/producer/consumer placement on CPUs
0/1/2 as experiment 003. The canonical configuration was a 64-byte payload and 1024 exact usable
slots. All checksums matched within each workload, every requested affinity was applied and
verified, perf policy remained 4, and thermal-throttle counters did not change.

The initial formal group used four three-trial ABBA commands. Command-level drift and ping-pong
bimodality motivated a finer repetition using one-trial commands. That repetition still showed
unstable cached-index behavior, so a final bounded group retained four one-trial ABBA blocks rather
than adding a parameter matrix or continuing repetitions mechanically. It used a 200,000,000-
message basic-throughput conditioning command, 20,000,000 measured and 2,000,000 warmup messages
for throughput, and 4,000,000 measured and 400,000 warmup exchanges for ping-pong. The shortest
timed row was 2.330 seconds. The final group retained eight rows per implementation and workload.

All earlier groups remain under `results/l1/l1b-scalar/` and `repetition/`; they were not silently
discarded or pooled with the final group because their trial grouping, conditioning, and ping-pong
iteration counts differ. A local analyzer path error made the final runner exit after all benchmark
commands and diagnostics had completed. The 32 final CSV files, stdout/stderr, event timestamps,
thermal and turbostat observations, and throttle counters are intact; the corrected ignored
analyzer subsequently validated and summarized every final CSV. The failed tail step only left the
completion footer absent from that group's sidecar.

## Compared variants

- `basic` acquires the remote index on every push and pop attempt.
- `cached-index` acquires the remote head only when its cached head makes the ring appear full and
  acquires the remote tail only when its cached tail makes the ring appear empty.

Both variants use the same default-constructed inline slots, compile-time exact usable capacity,
monotonic counters, modulo addressing, assignment-based payload lifetime, scalar non-blocking API,
and relaxed-own/acquire-remote/release-publish ordering. The cached variant necessarily adds two
non-atomic counter-sized members.

## Results

The final group's primary summaries pool all eight retained rows per implementation. Throughput
deltas are positive when `cached-index` is faster; ping-pong deltas are negative when its RTT is
lower.

| Workload | Basic median | Cached-index median | Delta | Basic sample CV | Cached-index sample CV |
| --- | ---: | ---: | ---: | ---: | ---: |
| Throughput | 8,045,038 msg/s | 8,299,573 msg/s | +3.164% | 0.911% | 2.663% |
| Ping-pong median RTT | 779.0 ns | 776.0 ns | -0.385% | 2.676% | 20.691% |

Throughput block deltas were +2.366%, +2.175%, +0.697%, and +3.473%. The direction was positive in
all four blocks, but the cached-index rows had an 8.669% full range, well above the L1A warm-state
noise and the basic rows' 2.699% range. The observed effect size is therefore not stable.

Ping-pong block deltas were -25.000%, +0.322%, -19.586%, and +0.063%. Cached-index median RTTs
alternated between fast and slow modes, producing a 44.716% full range. Fast and slow observations
occurred at similar approximately 73-76 C temperatures. Turbostat samples with both workers busy
reported a mean busy frequency of 3398.16 MHz, with 3389-3399 MHz observed. Placement remained
fixed and no throttle counter changed. The recorded host observations do not explain the modes.

## Interpretation

Cached remote indices showed about 3.2% higher median throughput in the final canonical repetition,
and every paired block was positive. The variant's much larger dispersion means this is a small,
directionally repeated observation whose magnitude is unstable, not a precise durable speedup.

The ping-pong result is inconclusive. Its pooled medians are nearly equal, while the cached-index
rows are strongly bimodal and the paired block effects disagree. The evidence does not support
ranking the variants for ping-pong or attributing the modes to thermal throttling, migration,
frequency behavior, cache effects, or coherence traffic.

## Limitations

The cached variant adds producer- and consumer-owned members, so object size, member placement,
compiler code generation, and cache-set mapping remain structural confounders alongside remote-load
frequency. The experiment does not count remote atomic loads or isolate coherence traffic. It
covers one CPU, compiler, same-node core pair, payload size, capacity, waiting strategy, and
saturated workload. More repetitions of the same protocol are not justified without a concrete
hypothesis for the observed modes. Ping-pong RTT is a round trip rather than exact one-way latency.

## Reproduction

After the fresh Release build and correctness gates shown in experiment 003, run the final
supporting protocol from a clean detached checkout of the measurement revision:

```sh
bench=./build/release/apps/handoff-bench/handoff-bench
result_dir=results/l1/l1b-scalar/cached-repetition
mkdir -p "$result_dir"

taskset -c 0 "$bench" run throughput \
  --implementation basic --payload-bytes 64 --capacity 1024 \
  --iterations 200000000 --warmup 2000000 --trials 1 \
  --producer-cpu 1 --consumer-cpu 2 \
  --output "$result_dir/precondition.csv"

for workload in throughput ping-pong; do
  if [ "$workload" = throughput ]; then
    iterations=20000000
    warmup=2000000
  else
    iterations=4000000
    warmup=400000
  fi
  for block in 1 2 3 4; do
    position=0
    for implementation in basic cached-index cached-index basic; do
      position=$((position + 1))
      taskset -c 0 "$bench" run "$workload" \
        --implementation "$implementation" --payload-bytes 64 --capacity 1024 \
        --iterations "$iterations" --warmup "$warmup" --trials 1 \
        --producer-cpu 1 --consumer-cpu 2 \
        --output "$result_dir/$workload-b$block-$position-$implementation.csv"
    done
  done
done
```

Capture the same host, power-policy, load, temperature/frequency, and throttle sidecars as
experiment 003, and preserve every row.
