# Experiment: Can the isolated N150 resolve scalar SPSC differences?

- Type: measurement-method characterization and complete-route comparison
- Status: running; busy-retry throughput remains too dispersed for a precise rate comparison
- Yield-throughput and yield-RTT revision: `9891fd0b5eb44cdcba96eb77b5d6d3c9b76ce5cd`
- Spin-RTT and yield-diagnostic revision: `7b3c299a46081ce0543958c0b3021aae2d78bc42`
- Busy-retry follow-up revision: `f9eea5152608d0fc5b1006f1958f186ac2195ffb`
- Date: 2026-10-01 (Asia/Shanghai)

## Question and boundary

Can the dedicated N150 profile repeat scalar `basic`, `cache-line`, and `cached-index` SPSC
completed-throughput and ping-pong RTT differences at fixed 2400 MHz? Are earlier ambiguous
results explained by host interference or by the waiting/timing behavior of the workload? These
three rings share exact-slot lossless FIFO, scalar assignment-based payload transfer, one producer,
one consumer, checksum validation, and the same timed boundaries. Their full object layouts and
remote-index access paths differ, so a rate difference does not isolate one atomic or cache line.
Throughput and RTT are separate axes; the result is conditional on each payload/capacity shape.

## Gate and procedure

Both measurement revisions were clean and CI-green (`36761030412` and `36806060014`). Native
Clang 21.1.8 Release passed 168/168 tests. The N150 has four physical cores without SMT and one
NUMA node. Kernel `7.0.0-34-generic` booted with CPUs 1-3 domain-isolated and full tick offload;
CPU 0 held the coordinator and housekeeping work. Producer and consumer were pinned to CPUs 1 and
2 with exact affinity readback. All four `intel_pstate` policies were saved, fixed at 2400 MHz,
read back, then restored. Turbo and thermal protection remained enabled.

For each materially different workload, a sustained representative qualification sampled worker
busy frequency, package temperature/power, IRQs, running threads, SMI, and thermal-throttle
counters. The 64 B / 1024-slot throughput and RTT groups and the 8 B / 64-slot groups passed their
applicable host checks. The 8 B throughput pilot with 80 million iterations ran only 1.99 s and
failed the three-full-window qualification rule; it was retained and replaced by an 800-million-
message qualification. No formal rows were taken from the failed pilot. A `turbostat` TSC-rate
warning appeared after some diagnostics; active-window busy-frequency samples remained valid, but
clock-read behavior still needs independent calibration.

Each formal group used 24 one-trial rows in four six-position, order-balanced blocks. The three
routes each appear twice per block. All raw CSV rows, stdout, global order, pre/post IRQ and
throttle snapshots, host sidecar, per-route qualification, ordered plots, and block-difference
plots are retained under ignored `results/spsc-cross-conditions-20261001/` and
`results/spsc-scalar-matrix-20261001/`. No anomalous row was excluded. The 256 B / 1024-slot
matrix was intentionally stopped during its first qualification when the high dispersion at 8 B
required diagnosis; its partial records are retained and are not comparative evidence.

## Observations

At 64 B / 1024 slots with yield retries, completed throughput had these eight-row summaries:

| Route | Median M/s | Range M/s | Sample CV |
| --- | ---: | ---: | ---: |
| basic | 6.081 | 6.029-6.513 | 2.96% |
| cache-line | 6.502 | 6.487-6.558 | 0.41% |
| cached-index | 6.056 | 5.997-6.126 | 0.70% |

Cache-line/basic block median differences were positive in all four blocks (+3.59% to +7.74%).
Cached-index/basic direction changed by block. The default yield-based RTT rows showed fast/slow
states: basic median RTT ranged 729-1383 ns, cache-line 706-1254 ns, and cached-index 1252-1357
ns. A fast row could have a roughly 720 ns median but roughly 1300 ns p95. The retained CSV
summarizes each row rather than storing all per-exchange samples, so the within-row switch point
cannot be reconstructed. These RTT rows do not rank routes.

With spin retries for ping-pong on the same revision's 64 B / 1024-slot shape, all three sustained
qualifications had 14 full-busy two-second windows per route at 2400 MHz; package power was
6.89-7.10 W and temperature 64-68 C. There was no worker device IRQ, SMI, or throttle delta.

| Route | Median RTT ns | Range ns | Sample CV | Median p95 ns |
| --- | ---: | ---: | ---: | ---: |
| basic | 587.0 | 565-640 | 5.36% | 601.5 |
| cache-line | 558.0 | 558-580 | 1.46% | 573.0 |
| cached-index | 713.5 | 694-727 | 1.56% | 731.5 |

All four cache-line/basic RTT block differences were negative (-0.69% to -12.74%); all four
cached-index/basic differences were positive (+13.45% to +23.68%). Basic's mode-like drift makes
the magnitude imprecise. This is a spin-RTT comparison, not a throughput result or exact one-way
latency.

At 8 B / 64 slots, yield-throughput qualifications passed after longer runs, but formal row
dispersion was much larger than small route effects:

| Route | Median M/s | Range M/s | Sample CV |
| --- | ---: | ---: | ---: |
| basic | 40.685 | 25.997-43.601 | 20.70% |
| cache-line | 28.428 | 12.596-31.360 | 21.86% |
| cached-index | 44.690 | 43.322-47.174 | 3.04% |

Within one block, basic rose from about 26 to 43.6 M/s, while one cache-line row fell to 12.6
M/s. These are retained outcomes, not invalidated host events. They cannot support a precise
throughput ranking. Spin-RTT at this shape was more repeatable for basic and cache-line: median
407 and 419 ns, CV 1.24% and 1.66%. Cached-index median RTT was 486 ns with 5.86% CV; its
block penalty relative to basic grew from +12.59% to +23.18%. Cache-line/basic RTT direction here
was +1.35% to +6.14%, opposite the 64 B / 1024-slot direction. Payload and capacity changed
together, so the sensitivity source cannot yet be separated.

A separate 24-row `perf stat` diagnostic at 8 B / 64 slots retained every 200-million-message
yield-throughput row and system-wide CPU-1/2 `sched_yield` and context-switch counts under the
fixed policy in `results/spsc-yield-diagnosis-20261001/`. Basic rate ranged 27.623-45.209 M/s,
cache-line 25.400-30.645 M/s, and cached-index 44.374-47.175 M/s. Each row counted roughly
3.0-4.6 million yield syscalls but only 16-20 context switches on the worker cores. Within the
basic and cache-line routes, higher yield counts correlated with *higher* rates in these eight
rows (Pearson +0.926 and +0.812), so the number of yield calls alone does not explain slow rows.
The `perf` rows are diagnostic because monitoring changes execution and was not accompanied by
per-row sustained `turbostat`; do not pool them with the formal rows.

## Interpretation and next gate

Boot isolation and fixed delivered busy frequency did not make all scalar SPSC measurements
stable. Device IRQ, SMI, throttle, worker migration, and unrelated runnable worker tasks were not
observed in the qualified groups. That narrows the search but does not establish a specific queue,
cache, compiler, or clock cause. The near-absence of context switches weakens the simple
preemption explanation; millions of yield syscalls and changing queue occupancy remain plausible
workload influences, not proven causes. The `cached-index` 8 B throughput stability also shows the
variation is route-dependent under the tested conditions.

The next formal step used the clean, CI-green busy-retry revision (`36814828867`, all five jobs
passed; native Release 168/168). All three 8 B / 64-slot throughput routes passed their separate
800-million-message qualifications: 16 full-busy two-second windows each, delivered 2400 MHz on
both pinned workers, 60-65 C package temperature, 6.16-6.31 W, and no worker device IRQ, SMI,
or throttle delta. A 24-row order-balanced throughput group then produced:

| Route | Median M/s | Range M/s | Sample CV |
| --- | ---: | ---: | ---: |
| basic | 14.328 | 13.732-17.019 | 9.17% |
| cache-line | 17.438 | 15.536-17.801 | 4.13% |
| cached-index | 11.304 | 8.089-11.880 | 13.69% |

The four cache-line/basic block median differences were +11.92%, +3.87%, +25.35%, and +22.96%;
the cached-index/basic differences were -28.44%, -27.02%, -28.91%, and -29.16%. These directions
are observed under busy retries, but the within-route spread, particularly for basic and
cached-index, does not support a precise performance gap. The rate and route ordering also differ
substantially from yield retries; the two policies describe different workloads and must not be
pooled. All 24 rows, their global order, per-row CSV, sidecar, host checks, and derived order and
block-difference plots are in `results/spsc-spin-reassessment-20261001/`; none was excluded.

The planned 8 B ping-pong and 64 B / 1024-slot groups were stopped during 8 B ping-pong
qualification once the throughput stability gate failed. The first 8 B ping-pong qualification
passed; the second was interrupted, and neither is formal RTT evidence. The interrupted files
remain in the same directory. The frequency profile was restored byte for byte to its saved
700-3600 MHz `powersave`/`balance_performance` policy. `turbostat` emitted a TSC-rate warning
after one qualification, although its active-window busy-frequency observations were 2400 MHz;
the independent [clock probe](024-clock-read-calibration.md) addresses clock-read cost separately.

The throughput timer reads once before releasing the timed phase and once after the consumer's
last validated item. A roughly 28 ns clock read cannot directly account for multi-second
between-row throughput changes. The coordinator-to-worker release is included once per row and
is negligible relative to the 11-25-second timed regions, but this does not rule out a workload
state change. Busy retries, queue occupancy, cache-line traffic, and intermittent interference
remain hypotheses; neither isolation nor these summaries identify the cause. Before expanding to
batch, sequence, fan-out, pipeline, or record layouts, use a focused diagnostic of retry and
occupancy states and repeat only after its perturbation has been assessed. For reliable fan-out,
one completed publication requires two observations; its numerator and role work differ from
single-consumer SPSC. Pipeline requires ordered upstream/downstream stages. They can be compared
as complete three-worker contracts after separate host qualification, not on a universal rate
axis.

The local scripts `canonical.sh`, `spin.sh`, matrix `run.sh`, and yield-diagnostic `run.sh` retain
the exact commands, save/restore sequence, raw rows, order, and analysis logic. Historical SHAs
are necessary for reproducing yield-based rows; current source is not a substitute for those
revisions.
