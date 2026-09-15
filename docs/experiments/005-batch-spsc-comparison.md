# Experiment: Does all-or-nothing batch publication change SPSC throughput?

- Type: mechanism isolation
- Status: complete
- Measurement revision: `25e04bc0189fb5bc6906b19cb962b8395137eee2`
- Date: 2026-09-14 to 2026-09-15

## Question

How does publishing fixed groups of messages with one counter update change steady-state SPSC
throughput relative to publishing the same messages individually?

## Hypothesis

Batch publication may reduce shared counter and ordering work per message, while all-or-nothing
availability may also change waiting granularity. The balance may depend on group size, capacity,
payload work, compiler decisions, and system noise.

## Setup

The controlled comparison ran on the physical four-core Intel N150 host under Ubuntu 26.04.1 with
Clang 21.1.8. The exact measurement revision was clean and green in all required CI jobs, then
passed fresh native Linux Release and clang-tidy builds and all 120 tests in each configuration.
The process was restricted to CPU 0; producer and consumer placement was applied and verified on
CPUs 1 and 2. The stock `intel_pstate` `powersave` governor, `balance_performance` EPP, boost state,
and perf policy 4 were unchanged.

The canonical configuration used a 64-byte payload, 1024 exact usable slots, 20,000,000 measured
messages, and 2,000,000 warmup messages. A 200,000,000-message basic command conditioned the
package before the initial group. A pilot confirmed that the same iteration count kept every timed
configuration above roughly two seconds.

Each group used one-trial ABBA blocks in the order `basic`, `batch`, `batch`, `basic`. Sizes 1 and 4
retained four blocks after the initial three showed enough variation to justify the contract's one
additional block. That supplement ran the next day from a quiet but cooler starting state without
repeating the long conditioning command; its separate sidecar and ABBA pairing are retained, and
the broader condition is part of the interpretation rather than being hidden. Size 16 retained the
original three blocks because its direction was already consistent. Every row was kept: 16 rows at
each of sizes 1 and 4 and 12 rows at size 16. All checksums matched, every benchmark stderr was
empty, the shortest timed row was 2.114 seconds, and thermal-throttle counters did not change.

## Compared variants

- `basic` generates each fixed group but calls scalar push and pop for every message, publishing
  each slot separately;
- `batch` generates the same group and uses one all-or-nothing push and pop, publishing the complete
  group once.

Both variants use identical inline slot storage, exact capacity, payload generation and validation,
yield waiting, total completed-message count, checksum work, blocked phase control, and conservative
acquire/release ordering. The group-size-one route checks the complete batch call shape even though
one publication still covers one message.

## Results

The primary summaries pool all retained rows for each implementation and size. Positive deltas mean
`batch` completed more messages per second.

| Group size | Basic median | Batch median | Delta | Basic sample CV | Batch sample CV |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 8,100,594 msg/s | 8,180,760 msg/s | +0.990% | 5.187% | 0.937% |
| 4 | 8,306,550 msg/s | 8,781,301 msg/s | +5.715% | 3.358% | 4.580% |
| 16 | 9,142,090 msg/s | 9,277,675 msg/s | +1.483% | 1.122% | 1.081% |

The basic/batch full ranges were 12.533%/2.672% at size 1, 10.374%/9.346% at size 4, and
2.845%/2.411% at size 16. Paired block deltas were -4.983%, +2.355%, +1.109%, and -4.706% at size
1; +5.845%, +5.397%, +0.768%, and +5.401% at size 4; and +1.295%, +2.716%, and +3.369% at size 16.
Raw CSV, stdout/stderr, sidecars, temperature/frequency observations, throttle counters, and analysis
remain in ignored `results/l1/l1b2-grouped/` on both hosts.

## Interpretation

Size 1 is inconclusive: the pooled median difference is only 0.990%, dispersion is high for the
basic route, and paired direction splits two positive and two negative blocks.

Size 4 produced a directionally repeated batch advantage, but its magnitude is variable. All four
blocks were positive and the pooled median delta was +5.715%, while both variants had several
percent CV and one block's +0.768% delta was near the host's observed variation. Size 16 produced a
smaller conditional positive observation: all three blocks were positive, with a +1.483% pooled
median delta and roughly 1.1% CV on both routes.

These are group-size-specific observations, not a publication-scaling curve. The compile-time
throughput route generates and validates fixed groups on both implementations, so changing the
group size also changes benchmark-side loop shape and compiler opportunities. Absolute throughput
at different sizes cannot isolate publication frequency.

## Limitations

The comparison covers one CPU, compiler, same-node placement, payload size, capacity, waiting
strategy, and saturated workload. It does not isolate an individual atomic or ordering operation,
measure partial-progress calls, establish a universal optimal group size, or cover ping-pong,
offered load, cross-NUMA placement, or multiple producers or consumers. No PMU data was collected,
and the timed observations do not establish a cache or instruction-count cause.

## Reproduction

From a clean detached checkout of the measurement revision after the fresh Linux gates:

```sh
bench=./build/release/apps/handoff-bench/handoff-bench
result_dir=results/l1/l1b2-grouped
mkdir -p "$result_dir"

taskset -c 0 "$bench" run throughput \
  --implementation basic --payload-bytes 64 --capacity 1024 --batch-size 1 \
  --iterations 200000000 --warmup 2000000 --trials 1 \
  --producer-cpu 1 --consumer-cpu 2 --output "$result_dir/precondition.csv"

for batch_size in 1 4 16; do
  for block in 1 2 3; do
    position=0
    for implementation in basic batch batch basic; do
      position=$((position + 1))
      taskset -c 0 "$bench" run throughput \
        --implementation "$implementation" --payload-bytes 64 --capacity 1024 \
        --batch-size "$batch_size" --iterations 20000000 --warmup 2000000 --trials 1 \
        --producer-cpu 1 --consumer-cpu 2 \
        --output "$result_dir/batch-b${batch_size}-block${block}-${position}-${implementation}.csv"
    done
  done
done

# Run this supplement separately on the next day from the quiet, cooler starting state; do not
# repeat the long conditioning command. Retain a separate host sidecar.
for batch_size in 1 4; do
  position=0
  for implementation in basic batch batch basic; do
    position=$((position + 1))
    taskset -c 0 "$bench" run throughput \
      --implementation "$implementation" --payload-bytes 64 --capacity 1024 \
      --batch-size "$batch_size" --iterations 20000000 --warmup 2000000 --trials 1 \
      --producer-cpu 1 --consumer-cpu 2 \
      --output "$result_dir/batch-b${batch_size}-block4-${position}-${implementation}.csv"
  done
done
```

The retained sidecars record the exact host state, load, policy, placement, temperature/frequency,
and throttle-counter deltas for the initial and supplemental groups.
