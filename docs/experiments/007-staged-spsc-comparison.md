# Experiment: What changes when SP/SC slot access is staged?

- Type: mechanism isolation
- Status: complete
- Measurement revision: `25e04bc0189fb5bc6906b19cb962b8395137eee2`
- Date: 2026-09-14

## Question

For equal fixed message groups, how does reserving ring slots and accessing them directly before one
finish operation differ from an all-or-nothing bulk operation that transfers through intermediate
payload arrays?

## Hypothesis

Staged access may reduce intermediate assignment work while adding a move-only token lifecycle and
span traversal. The balance may depend on payload size, requested group size, physical wrap,
compiler decisions, and system noise.

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
| ---: | ---: | ---: | ---: | ---: | ---: |
| 9,444,096 msg/s | 9,897,072 msg/s | +4.796% | 0.971% | 0.157% |

The full ranges were 2.549% and 0.496% of the respective medians. Paired block deltas were +4.970%,
+4.742%, and +3.629%, so staged was faster in all three blocks. Raw evidence remains with the batch
comparison in ignored `results/l1/l1b2-grouped/` on both hosts.

## Interpretation

For this N150, Clang build, canonical payload/capacity, group size, placement, and saturated
workload, the complete staged route had a clear conditional throughput advantage over bulk. The
effect held in every paired block and exceeded the measured dispersion, while the staged rows were
especially stable.

This result supports direct span access as a useful grouped-operation variant under the measured
conditions. It does not isolate which removed assignment, span traversal, token operation, or
compiler decision caused the difference.

## Limitations

The experiment covers one group size because size 16 answered the bounded question without a
sensitivity run. It covers one CPU, compiler, same-node placement, payload size, capacity, waiting
strategy, and saturated workload. It does not establish end-to-end no-copy behavior, DPDK API or
ABI compatibility, partial finish or cancellation costs, cross-NUMA behavior, or multi-producer or
multi-consumer behavior. No PMU data was collected.

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
