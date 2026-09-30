# Experiment: Fixed-frequency MPSC completion comparison

- Type: complete-implementation comparison
- Status: complete
- Measurement revision: `4f33634efe0c4d3a97040aba866fed1d2afe3317`
- Date: 2026-10-01 (Asia/Shanghai)

## Question and boundary

Does the `mpsc-slot` versus `mpsc-count` complete-handoff rate direction from
[Experiment 016](016-mpsc-producer-completion.md) persist in an interleaved run
when the N150 delivers a verified fixed 2400 MHz under both workloads? This
tests two comparable FIFO, exact-slot, two-producer/one-consumer routes. It
does not rank the catalog, isolate a particular atomic or cache transition,
or compare frequency profiles.

The two routes use the same position-derived 64-byte payload, claim work,
yield retries, scalar workload, consumer FIFO validation and checksum, and
timed end boundary at final validated consumer handoff. Count publication
uses a shared completion RMW and group tail; slot publication release-marks
a generation tag for consumer discovery. Their progress contracts differ
across holes, so a rate difference is a whole-route observation.

## Gate and host

The clean measurement SHA was CI-green in run `36750392223` (Linux and macOS
Release, format/tidy, ASan/UBSan, TSan). The local native Clang 21.1.8 Release
build used `-O3 -DNDEBUG` and passed 168/168 tests before measurement.
The host was an Intel N150, four physical cores without SMT, one NUMA node,
Ubuntu 26.04.1, kernel `7.0.0-34-generic`. Boot isolation was
`isolcpus=domain,managed_irq,1-3 nohz_full=1-3 irqaffinity=0`; the workqueue
mask was CPU 0. The coordinator ran under `taskset -c 0`; producer 0, producer
1, and consumer requested CPUs 1, 2, and 3, and every retained CSV read back
those exact one-CPU masks. The wired IRQ stayed on CPU 0; the managed NVMe
worker-CPU IRQ counters were unchanged during qualification and formal rows.

All four `intel_pstate` policies were saved at 700-3600 MHz, `powersave`, and
`balance_performance`, then read back at 2400-2400 MHz, `performance`, and
`performance`. Turbo and thermal protection remained enabled; RAPL long and
short limits were 20 and 25 W. The original policy was restored and matched
byte for byte after the session.

Each route first ran 120 million timed messages with 2 million warmup while
`turbostat` sampled at 2 seconds. Twelve full-busy `count` windows and nine
full-busy `slot` windows had all three workers above 95% busy; every worker
busy-frequency value was 2400 MHz (declared tolerance +/-1%). Package
temperature was 65-68 C for count and 66-68 C for slot; power was 7.77-7.79
and 7.85-7.88 W respectively. Two running-thread snapshots per diagnostic
showed only benchmark threads runnable on CPUs 1-3. No worker device IRQ,
SMI, or thermal-throttle counter increased. `turbostat` may briefly execute
on a worker CPU while sampling; it was used only in these diagnostic runs,
not in formal rows. These checks qualify a cautious complete-route comparison
for this host and placement, not fine coherence attribution.

## Procedure and results

A 40-million-message `mpsc-slot` run conditioned the host. Each retained
command used 2 million warmup and 20 million timed messages, one trial, 64 B,
1024 native slots, and CPUs 1/2/3. A single CSV was written per command;
all 16 validated checksum `3086954737262792784`. The four blocks alternated
`C A A C`, `A C C A`, `C A A C`, `A C C A`, where C is count and A is slot.
Adjacent positions 1-2 and 3-4 form eight order-balanced pairs. Values below
are million completed handoffs per second in global execution order.

| Block | Position 1 | Position 2 | Position 3 | Position 4 |
| --- | ---: | ---: | ---: | ---: |
| 1 | C 4.385 | A 5.898 | A 5.939 | C 4.738 |
| 2 | A 5.926 | C 4.410 | C 4.551 | A 6.084 |
| 3 | C 4.719 | A 6.036 | A 6.000 | C 4.738 |
| 4 | A 6.017 | C 4.876 | C 4.463 | A 6.526 |

| Route | Median M/s | Full range M/s | Sample CV |
| --- | ---: | ---: | ---: |
| Count | 4.635 | 4.385-4.876 | 3.94% |
| Slot | 6.008 | 5.898-6.526 | 3.32% |

The eight adjacent `(slot/count - 1)` differences in order were +34.52%,
+25.36%, +34.38%, +33.69%, +27.91%, +26.64%, +23.40%, and +46.24%; their
median was +30.80%. Every pair favored slot. The final pair combines a
lower count row and the highest slot row, producing the +46.24% endpoint.
It has no observed external invalidation and was retained. The formal group
had no worker device IRQ or thermal-throttle counter delta; those endpoints
cannot rule out every transient disturbance.

This supports the direction of a roughly 30% complete-route advantage for
slot in this controlled N150 workload. The exact effect size is less stable
than its direction, and this observation does not identify a cause or predict
other payloads, capacities, producer counts, machines, or failure behavior.
Busy frequency was sampled during sustained qualification of both actual
routes, not during every short formal row. The read-back fixed limits and
absence of throttling bound that gap but do not prove every row's delivered
frequency. Compared with Experiment 016, kernel and frequency policy also
changed; the two experiments must not be pooled into a frequency effect.

## Reproduction and retained evidence

Raw CSVs, stdout, global `order.csv`, host sidecar, per-route 2-second
`turbostat` data, IRQ/throttle snapshots, qualification assessments, analysis
script, and generated order and paired-difference SVGs are retained in ignored
`results/controlled-mpsc-2400-20261001/`. Earlier interrupted diagnostics
remain there separately and are not part of the 16-row formal set. The SVGs
and summaries were generated from the retained per-command CSV rows; no row
was filtered.

The exact session commands and save/restore procedure are in local `run.sh`.
After a clean native Release build and correctness gate at the measurement
revision, the formal command shape was:

```sh
taskset -c 0 ./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation mpsc-count \
  --payload-bytes 64 --capacity 1024 --iterations 20000000 \
  --warmup 2000000 --trials 1 --producer-cpus 1,2 --consumer-cpu 3 \
  --output ROW.csv
```

Substitute `mpsc-slot` and execute in the block order above. The qualification
used the same command shape with 120 million iterations while a separate
privileged `turbostat` sampled both routes. Recheck the host and the exact
revision before treating a rerun as controlled evidence.
