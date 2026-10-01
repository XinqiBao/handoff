# Experiment: Does the MPSC direction persist under busy retries?

- Type: complete-route implementation comparison
- Status: complete
- Measurement revision: `b87f9cf41d73412570159378fc68b2a7e7ad9537`
- Date: 2026-10-01 (Asia/Shanghai)

## Question and boundary

Does the `mpsc-slot` over `mpsc-count` direction from
[Experiment 022](022-fixed-frequency-mpsc-comparison.md) remain clearly resolvable
under the current canonical busy-retry throughput workload? The question concerns
a conditional complete-route direction, not a permanent percentage, an isolated
atomic/cache/publication cost, or a catalog ranking. Historical yield rows are
context and are not pooled with the new workload.

Both routes complete lossless global claim-order FIFO delivery through 1024 exact
native slots. Claims remain charged until consumer release; unfinished claims
retain capacity. Two producers each generate half the position-derived 64-byte
payloads through scalar CAS claims and direct slot assignment. The sole consumer
checks every position and payload, updates the same checksum, and releases each
slot. The numerator is 20 million completed handoffs. Timing runs from the
coordinator's clock immediately before timed phase release through the consumer's
clock after its final validation and release. Allocation, placement, warmup,
thread joins, final expected-checksum computation, and file output are excluded.
Coordinator synchronization uses atomic waits; unavailable timed claims and
observations busy spin.

Publication return can precede FIFO visibility in both routes. Count uses a shared
completion RMW and conditional group-tail CAS; a newer unfinished claim can hide
an earlier ready prefix. Slot release-marks each generation and lets the consumer
discover its next ready position. Layout, footprint, visibility, contention, and
retry interactions differ. Equal slot capacity does not mean equal storage bytes.
Neither route recovers abandoned owners or supplies a general fairness or
operation-wide lock-free guarantee.

## Gate and host

The clean measurement revision passed all five jobs in CI run `36871924346`:
Linux/macOS Release, format/clang-tidy, ASan/UBSan, and TSan. Native Clang
21.1.8 Release (`-O3 -DNDEBUG`) passed 168/168 tests before measurement.
No mechanism or harness source changed in this campaign.

The Intel N150 has four physical cores without SMT, one NUMA node, and shared
L2/L3. Ubuntu 26.04.1 booted kernel `7.0.0-34-generic` with
`isolcpus=domain,managed_irq,1-3 nohz_full=1-3 irqaffinity=0`. The workqueue
mask was CPU 0. Every qualification and formal CSV read back exact producer
masks 1/2 and consumer mask 3; the process/coordinator ran under `taskset -c 0`.
IRQ effective masks and cache topology were captured. Managed NVMe IRQs with
worker masks had no counter increase during qualification or formal collection.

All four `intel_pstate` policies were saved at 700-3600 MHz, `powersave`,
`balance_performance`; requested and read back at 2400-2400 MHz,
`performance`/`performance`; then restored byte for byte. Turbo and thermal
protection stayed enabled. Package long/short power limits were 20/25 W.
Two-second `turbostat` samples were collected in separate sustained diagnostic
runs of each actual route. The declared busy-frequency tolerance was +/-1%;
every worker value in full-busy windows was 2400 MHz.

| Route | Full-busy 2 s windows | Package temperature | Package power |
| --- | ---: | ---: | ---: |
| Count | 14 | 62-63 C | 7.14-7.21 W |
| Slot | 10 | 63-64 C | 7.17-7.21 W |

All three workers were at least 95% busy in those windows. Final temperatures
were stable. Two runnable-thread snapshots per route found only the expected
benchmark workers running on CPUs 1-3. Worker device IRQ, SMI, and thermal-throttle
checks passed. Formal collection had no worker device IRQ or throttle-counter
delta. Monitoring ran only during qualification; finite checks do not prove
that every formal row was interruption-free or delivered exactly 2400 MHz.

## Protocol and ordered results

Each route uses 2 million warmup. Sustained 120-million-message qualifications
sample both actual routes; formal runs have no turbostat or PMU collection.
A 40-million-message slot run conditions the host before 16 independent one-trial
process launches, in four alternating `C A A C` / `A C C A` blocks. C is count and
A is slot. Adjacent positions form eight order-balanced pairs. CPU 0 holds the
coordinator and housekeeping; producers use CPUs 1/2 and consumer CPU 3.

Rates are million completed handoffs per second, in global execution order.
All 16 commands validated checksum `3086954737262792784`; timed durations were
3.197-5.071 seconds. No row was excluded.

| Block | Position 1 | Position 2 | Position 3 | Position 4 |
| --- | ---: | ---: | ---: | ---: |
| 1 | C 3.944 | A 6.256 | A 5.558 | C 4.729 |
| 2 | A 5.439 | C 4.102 | C 4.061 | A 6.054 |
| 3 | C 4.146 | A 5.617 | A 5.699 | C 3.951 |
| 4 | A 5.546 | C 4.205 | C 4.195 | A 6.217 |

| Route | Median M/s | Full range M/s |
| --- | ---: | ---: |
| Count | 4.124 | 3.944-4.729 |
| Slot | 5.658 | 5.439-6.256 |

Adjacent paired `(slot/count - 1)` differences in execution order were
+58.63%, +17.55%, +32.58%, +49.06%, +35.48%, +44.25%, +31.89%, +48.20%.
Their median was +39.86%; all eight favored slot, across both
execution orders. The highest slot and lowest count rows occurred together in
the first pair (+58.63%). The next pair combined the highest count row
with a lower slot row (+17.55%). Neither has an observed invalidation;
both are retained. No universal CV or automatic significance gate was used.

## Interpretation and checkpoint

The prior slot-over-count direction remains clearly resolvable under this
canonical busy-retry workload on the qualified N150: all adjacent differences
are positive and even the full route ranges do not overlap. The substantial
paired magnitude variation prevents a precise enduring percentage claim.
This answers the stated question; no further MPSC diagnostics or parameter
matrix were run. The result does not isolate publication, atomic, cache, or
coherence cost, establish fairness/failure recovery, predict another host or
workload, or measure a yield-to-spin causal effect. Experiment 022 remains
separate evidence at its own revision and waiting policy.

A separately scoped `spmc-slot` versus `spmc-serialized` comparison has a concrete
next question: does overlapping consumer ownership/processing retain a large
complete-route advantage over whole-operation serialization under busy retries?
[Experiment 017](017-spmc-consumer-coordination.md) suggests a large effect,
while its anomalous sensitivity row limits magnitude expectations. Current routes
share work-sharing delivery, payload/seen-count validation, worker accounting,
exact slots, and producer-verified final reusable-prefix timing. They differ in
mutex admission, processing overlap, release, and reuse discovery; mutex blocking
remains part of the serialized mechanism despite busy harness retries. These
facts support a future complete-route question, not isolated release-cost attribution.
It needs separate actual-route qualification on producer CPU 1 and worker CPUs
2/3. It was reassessed here and deliberately not executed. Adding `spmc-ordered`
would need a distinct information-bearing question, not a three-way ranking.

The campaign's repository-health review found recurring roadmap/index edits in
recent measurement history, rather than recurring hot-path changes. The lighter
reference/supporting navigation and linked current boundary address that drift.
The route catalog and explicit workload dispatch still fit the small project;
no archival tooling, statistics framework, dependencies, or generic harness
abstraction is justified by this checkpoint.

## Reproduction and evidence

Working rows, qualifications, and diagnostics were collected under ignored
`results/controlled-mpsc-busy-20261001/`. The ordered values above preserve the
comparison in the tracked record.
After a clean native Release build and correctness gate at the measured SHA:

```sh
taskset -c 0 ./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation mpsc-count --payload-bytes 64 --capacity 1024 \
  --iterations 20000000 --warmup 2000000 --trials 1 \
  --producer-cpus 1,2 --consumer-cpu 3 --output ROW.csv
```

Substitute `mpsc-slot` and follow the recorded four-block order. Qualification
uses the same options with 120 million iterations and external two-second
`turbostat`; conditioning uses slot with 40 million iterations. The local
`run.sh` implemented host save/readback/restore and diagnostics; `analyze.py` checked metadata,
placement, workload, checksums, rate arithmetic, order, and host snapshots before deriving summaries.
Recheck live host controls and delivered frequency before any controlled rerun.
