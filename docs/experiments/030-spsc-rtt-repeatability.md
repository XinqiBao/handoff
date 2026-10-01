# Experiment: Ordered SPSC RTT samples and process repeatability

- Type: measurement-method characterization and bounded complete-route comparison
- Status: complete for 8 B / 64 slots; throughput state cause remains unresolved
- Measured revision: `0b434166537e5a9c4b615534ce00b44f176a2ddb`
- Date: 2026-10-01 (Asia/Shanghai)

## Question and boundary

Can the current ping-pong route retain each RTT in execution order without a consistent output
effect, and do independently launched `basic` and `cached-index` processes repeat closely enough
for an 8 B / 64-slot comparison? Both are lossless scalar SPSC rings with one producer and one
consumer, the same payload generation and validation, busy retries, and two-clock-read RTT
boundary. Their counter layouts and remote-progress logic differ. This measures a request plus
response through two rings with at most one request outstanding, not one-way or saturated queueing
latency. It does not resolve the separate completed-throughput process states.

## Gate and procedure

The exact revision had a clean worktree, five green CI jobs (run `36856051927`), a native Clang
21.1.8 Release build, and 168/168 native Release tests. The N150 retained its boot-isolated
CPU-1/2 worker profile; CPU 0 held housekeeping and the coordinator. The session saved all four
cpufreq policies, requested and read back 2400 MHz, then restored the saved policy exactly.

Each route ran a separate 40-million-exchange, 100,000-warmup qualification with one-second
`turbostat` samples under its actual busy-spin workload. `basic` had 17 full-busy windows,
2400 MHz worker busy frequency, 60-63 C, and 6.57-6.62 W. `cached-index` had 18 such windows,
2400 MHz, 62-64 C, and 6.73-6.79 W. Exact CPU affinities, worker runnable-thread snapshots,
worker device IRQs, SMI, and thermal-throttle counters passed the recorded gate. The formal
group had no worker device IRQ or throttle delta. These finite checks do not prove every RTT
sample was interruption-free.

Four adjacent process pairs alternated `basic` raw-output off/on, then eight adjacent pairs
alternated `basic`/`cached-index`, both with raw output on. Each process ran 100,000 warmup and
2 million timed exchanges. `--latency-samples` writes each in-memory `trial,index,rtt_ns` value
after that trial's timing, before its next trial. The analysis read all 40 million retained
ordered RTT values and recomputed each on-mode trial's median, indexed p95, and indexed p99
exactly. It also retained 20,000-sample ordered windows. No row or outlier was excluded.

The first qualification attempt stopped before formal rows. It had run the benchmark through
root-owned `turbostat`, so Git's user-level `tags` ignore rule did not apply and the CSV reported
`git_dirty=true` despite the user's clean worktree. That monitor form also emitted only a total
window. Its CSV, host snapshots, fixed/restored policies, and failure are retained under
`results/spsc-rtt-metric-20261001/qualification-attempt-1/`. The corrected attempt ran the
benchmark as the repository user and monitored independently as root; it passed the gates above.

## Observations

| Group | Route or mode | Processes | Median of process RTT medians | Range | Process CV |
| --- | --- | ---: | ---: | ---: | ---: |
| Raw-output control | off | 4 | 409.0 ns | 403-417 ns | 1.72% |
| Raw-output control | on | 4 | 415.5 ns | 407-417 ns | 1.14% |
| Route comparison | basic | 8 | 409.0 ns | 403-416 ns | 1.20% |
| Route comparison | cached-index | 8 | 423.0 ns | 415-469 ns | 4.18% |

Output-on minus output-off median RTT differences were +13, +14, -10, and 0 ns. Their median
was +6.5 ns, with 2/4 positive. The paired p95 differences ranged -2 to +16 ns and p99
differences -43 to +60 ns. This small control found no consistent signed output effect; it is
not a bound proving that optional output has zero scheduling, memory, or tail effect.

In the route group, `cached-index` minus `basic` median RTT differences were +30, +2, +8, +14,
+7, +26, 0, and +64 ns. Their paired median was +11 ns, but the 0-64 ns span rules out a precise
small median gap. The corresponding p95 differences were positive in 8/8 pairs, with a median
of +69 ns and a +55 to +145 ns range. The p99 differences were also positive in 8/8 pairs,
with a median of +105.5 ns and a +61 to +211 ns range. This supports a conditional direction
for the two-ring RTT tail under this exact workload, not a stable effect size or one-way delay.

The raw order distinguishes transient outliers from a sustained process condition. For example,
`cached-index` row 23 had a 469 ns median in all 100 consecutive 20,000-sample windows, while
the following `basic` row stayed between 405 and 407 ns. `cached-index` row 10 changed during
the process: 52/100 windows had medians at least 450 ns. Thus its process spread cannot be
explained solely by a few exceptional samples or by reducing each process to one summary.

Ignored `results/spsc-rtt-metric-20261001/` retains the exact scripts, executable hash, sidecar,
all process CSVs and raw samples, global order, 20,000-sample windows, paired-difference CSVs,
ordered and paired SVGs, qualification streams, host snapshots, and both policy snapshots. The
514 MB raw set is kept for independent reanalysis. No historical result was removed.

## Interpretation and next gate

Completed throughput remains a valid measure of the whole fixed producer-to-consumer workload:
the producer's generation and retries, queue behavior, and consumer's validation all influence
it. Its one long elapsed interval amortizes clock reads, but cannot identify a queue-only cost or
locate within-process rate changes. Existing independent-process 8 B / 64-slot throughput states
remain unexplained; a precise throughput ranking is still blocked.

The RTT timer reads twice per exchange, and the earlier 28 ns `steady_clock` adjacent-read floor
is material at roughly 400-470 ns. The clock floor is not subtracted because it does not capture
full call placement or tail perturbation. A one-way or specified-offered-rate latency claim needs
a distinct workload and clock validation. Before broadening the SPSC matrix, isolate a concrete
source of the sustained `cached-index` state, and check producer/consumer work-limit controls for
throughput. `basic` and `cached-index` RTT tail direction may guide that diagnosis; it does not
make all SPSC latency comparisons stable.
