# Experiment: What changes when SP/SC slot access is staged?

- Type: mechanism isolation
- Status: complete
- Throughput measurement revision: `25e04bc0189fb5bc6906b19cb962b8395137eee2`
- PMU measurement revision: `0e884dfe9cf4bb173b0536b3405af1562a816a87`
- Dates: 2026-09-14 through 2026-09-15

## Question

For equal fixed message groups, how does reserving ring slots and accessing them directly before one
finish operation differ from an all-or-nothing bulk operation that transfers through intermediate
payload arrays?

## Hypothesis

Staged access may reduce intermediate assignment work while adding a move-only token lifecycle and
span traversal. The balance may depend on payload size, requested group size, physical wrap,
compiler decisions, and system noise.

After the stable throughput observation, L1E tested the narrower hypothesis that the staged route
executes fewer worker instructions per completed message than bulk.

## Setup

This comparison shared experiment 005's clean CI-green measurement revision, fresh Linux Release
and tidy gates, physical Intel N150 host, stock HWP policy, and verified coordinator/producer/
consumer placement on CPUs 0/1/2. It used a 64-byte payload, 1024 exact usable slots, group size 16,
20,000,000 measured messages, and 2,000,000 warmup messages after the common warm-state
conditioning command.

Three one-trial ABBA blocks ran in the order `bulk`, `staged`, `staged`, `bulk`, retaining six rows
per implementation. All checksums matched, every benchmark stderr was empty, the shortest timed row
was 2.016 seconds, perf policy remained 4, and thermal-throttle counters did not change. The
consistent size-16 result did not justify the optional size-4 sensitivity.

The selective PMU follow-up used the later clean CI-green revision shown above and the same payload,
capacity, group size, CPU placement, compiler, Release build, waiting behavior, and stock HWP
policy. A bounded pilot first measured the interval from worker discovery through a 40-million-
message warmup, verified exact producer and consumer TIDs from their `/proc` masks, and established
that a five-second attachment window could fit inside a deliberately long timed phase.

Formal collection used 40 million warmup and 70 million measured messages. Three one-trial ABBA
blocks retained six benchmark rows and six PMU windows per implementation. Each command waited for
the package sensor to cool before launch. Five seconds after worker discovery, `perf stat` attached
only to the verified producer and consumer TIDs for a fixed five-second middle window and collected
only `cycles:u` and `instructions:u`. Every event ran 100% of its enabled time, every benchmark was
still alive after attachment and completed with valid checksum and affinity metadata, benchmark and
perf stderr were empty, the shortest timed phase was 7.081 seconds, thermal-throttle counters did
not change, and perf policy was restored from 2 to 4. Two earlier hotter method runs that changed
throttle counters were retained locally but excluded before formal analysis.

## Compared variants

- `bulk` generates a group in a local array, copy-assigns it into ring slots, move-assigns it into a
  consumer array, and publishes or releases once per complete group;
- `staged` reserves the same complete group, generates payloads into writable ring spans, validates
  through const consumer spans, and explicitly finishes once on each side.

Both use default-constructed fixed slots, exact capacity, all-or-nothing group progress, one
producer and one consumer, the same payload generation and observation functions, final message
count, checksum, yield waiting, blocked phase control, and conservative acquire/release ordering.

## Results

| Bulk median | Staged median | Delta | Bulk sample CV | Staged sample CV |
| ---: | ---: | ---: | ---: | ---: |
| 9,444,096 msg/s | 9,897,072 msg/s | +4.796% | 0.971% | 0.157% |

The full ranges were 2.549% and 0.496% of the respective medians. Paired block deltas were +4.970%,
+4.742%, and +3.629%, so staged was faster in all three blocks. Working output was collected with the batch
comparison under ignored `results/l1/l1b2-grouped/`.

The PMU follow-up reproduced the throughput observation and reported aggregate worker counts. Raw
and scaled medians are identical because neither event was multiplexed. `counts/s` uses the fixed
PMU wall window. The per-message columns divide those window rates by complete-trial throughput;
they are approximate normalization, not exact counts for messages completed inside the PMU window.

| Metric | Bulk median | Staged median | Delta | Bulk sample CV | Staged sample CV |
| --- | ---: | ---: | ---: | ---: | ---: |
| Throughput | 9,414,543 msg/s | 9,882,086 msg/s | +4.966% | 1.002% | 0.101% |
| Raw/scaled cycles per window | 19,336,129,866 | 19,337,028,436 | +0.005% | 0.170% | 0.265% |
| Raw/scaled instructions per window | 49,899,701,469 | 52,413,309,954 | +5.037% | 1.006% | 0.094% |
| Aggregate worker cycles/s | 3,834,861,304 | 3,839,848,793 | +0.130% | 0.238% | 0.228% |
| Aggregate worker instructions/s | 9,905,324,667 | 10,404,653,434 | +5.041% | 1.061% | 0.145% |
| IPC | 2.583 | 2.709 | +4.888% | 0.898% | 0.270% |
| Approximate cycles/message | 407.130 | 388.916 | -4.474% | 0.847% | 0.237% |
| Approximate instructions/message | 1,051.537 | 1,052.944 | +0.134% | 0.121% | 0.074% |

Staged throughput was higher in all three PMU blocks by +3.597%, +4.782%, and +5.100%.
Instructions/s increased in the corresponding direction by +3.775%, +4.781%, and +5.341%.
Approximate instructions/message changed by +0.173%, -0.001%, and +0.230%, while approximate
cycles/message fell by -3.090%, -4.518%, and -4.790% and IPC rose by +3.361%, +4.731%, and
+5.272%.

## Interpretation

For this N150, Clang build, canonical payload/capacity, group size, placement, and saturated
workload, the complete staged route had a clear conditional throughput advantage over bulk. The
effect held in every paired block and exceeded the measured dispersion, while the staged rows were
especially stable.

This result supports direct span access as a useful grouped-operation variant under the measured
conditions. It does not isolate which removed assignment, span traversal, token operation, or
compiler decision caused the difference.

The targeted counters weaken the narrower explanation that staged wins by executing fewer
instructions per completed message. Staged retired about 5% more worker instructions per second as
it completed about 5% more messages per second; the approximate per-message instruction medians
were nearly equal, and their small +0.134% difference was not consistently directed by block.

The lower approximate cycles/message and higher IPC were consistent in all three blocks and are
compatible with staged doing similar retired instruction work more efficiently on this CPU. They do
not identify a source-level cause. The routes still differ in intermediate assignments, span
traversal, token operations, generated code, and scheduling interactions; retired instructions and
cycles alone cannot assign the throughput effect to one of them.

## Limitations

The experiment covers one group size because size 16 answered the bounded question without a
sensitivity run. It covers one CPU, compiler, same-node placement, payload size, capacity, waiting
strategy, and saturated workload. It does not establish end-to-end no-copy behavior, DPDK API or
ABI compatibility, partial finish or cancellation costs, cross-NUMA behavior, or multi-producer or
multi-consumer behavior.

The PMU follow-up covers one five-second worker window per command rather than the exact benchmark
timed boundary. Its per-message values combine middle-window event rates with complete-trial
throughput, so they are useful bounded evidence but not exact attribution. Counts aggregate the two
worker TIDs and do not separate producer from consumer. User-mode cycles and instructions do not
measure coherence traffic, explain compiler decisions, or establish causality. The result remains
limited to one CPU, compiler, payload, capacity, group size, placement, and saturated workload.

## Reproduction

After the build, gates, and conditioning command shown in experiment 005:

```sh
bench=./build/release/apps/handoff-bench/handoff-bench
result_dir=results/l1/l1b2-grouped

for block in 1 2 3; do
  position=0
  for implementation in bulk staged staged bulk; do
    position=$((position + 1))
    taskset -c 0 "$bench" run throughput \
      --implementation "$implementation" --payload-bytes 64 --capacity 1024 \
      --batch-size 16 --iterations 20000000 --warmup 2000000 --trials 1 \
      --producer-cpu 1 --consumer-cpu 2 \
      --output "$result_dir/staged-b16-block${block}-${position}-${implementation}.csv"
  done
done
```

The common sidecar records exact provenance, host and load state, verified placement, stock policy,
temperature/frequency observations, and throttle-counter deltas.

The PMU follow-up used the same command shape with 40,000,000 warmup and 70,000,000 measured
messages. For each one-trial ABBA command, the local runner discovered the TIDs whose exact allowed
masks were CPUs 1 and 2, waited five seconds after discovery, and attached:

```sh
perf stat --timeout 5000 --no-big-num --no-scale -x, \
  -e cycles:u,instructions:u -t "$producer_tid,$consumer_tid"
```

PMU working output was collected under ignored `results/l1/l1e-pmu/`, including the excluded
hot method runs. The protocol and limitations above preserve the useful method.

## Current applicability

This record describes its stated revision and conditions. The later correctness campaign restricts
wrapping fixed-slot SPSC families to power-of-two slot capacities so physical mapping remains valid
at machine-counter rollover. Ordinary physical wrap in these historical tests did not exercise that
rollover. Existing benchmark capacities remain supported, but these measurements are not renewed
evidence for the current implementation.
