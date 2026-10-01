# Experiment: Does initial occupancy select the SPSC rate state?

- Type: measurement-method diagnostic, not a route ranking
- Status: complete for empty/full initialization; cross-process cause unresolved
- Source revision: `92616413fff76c7a9600db665d4032c0b6c17594`
- Date: 2026-10-01 (Asia/Shanghai)

## Question and boundary

The [process-state](025-spsc-process-state-diagnostic.md),
[PMU](026-spsc-role-pmu-diagnostic.md), and
[address](027-spsc-address-state-diagnostic.md) diagnostics found persistent
`cached-index` rate states across independent processes under dedicated-core busy retries.
Can the queue's occupancy at the timed-phase boundary select one of those states? This tests
one causal workload condition, not a general occupancy-response curve or cross-route ranking.

## Protocol and controls

The clean revision above passed all five CI jobs (run `36837443399`); its unchanged native
Release suite had passed 168/168 tests. A local Clang 21.1.8 `-O3 -DNDEBUG` diagnostic reused
the actual `CachedIndexBoundedRing`, 8-byte payload generation, per-message validation, and
checksum, with 64 slots, producer on CPU 1, consumer on CPU 2, and coordinator on CPU 0.
Every row completed 2 million warmup plus 100 million timed validated messages in ten raw
segments. The queue page offset was zero. Local retry counts and segment clocks make rates
diagnostic rather than canonical throughput results.

After warmup and before releasing the timed workers, the coordinator either left the queue
empty or inserted all 64 valid first-phase messages. In the full condition the timed producer
then generated positions 64 through 99,999,999; the consumer still validated positions zero
through 99,999,999. The first segment records 9,999,936 timed producer operations and
10,000,000 consumer operations; later segments have equal work. This intentional first-segment
work difference is only 64 operations in 100 million, so the last nine segments were separately
summarized. The probe verified all messages and the final checksum in both conditions.

Each condition passed its own sustained actual-workload qualification after all four cpufreq
policies were saved, requested and read back at 2400 MHz. Empty start passed 27 full-busy
windows at 61-63 C and 6.17-6.30 W; full start passed 33 at 60-64 C and 6.14-6.38 W.
Worker affinities, runnable-thread snapshots, device IRQ, SMI, and thermal-throttle checks
passed. Two independent processes then each ran 16 rows in four balanced `empty, full,
full, empty` / `full, empty, empty, full` blocks. Both processes' raw rows, order,
qualifications, and before/after host checks were retained. The saved cpufreq policy was
restored exactly after the session.

## Observations

| Process | Empty median / CV | Full median / CV | Full/empty block differences |
| --- | --- | --- | --- |
| A | 13.444 M/s / 0.06% | 13.441 M/s / 0.02% | -0.03%, -0.03%, +0.05%, -0.02% |
| B | 11.604 M/s / 0.14% | 11.607 M/s / 0.09% | -0.13%, -0.06%, +0.09%, -0.06% |

The last-nine-segment medians matched the complete-row pattern: A's empty/full values were
13.444/13.441 M/s, while B's were 11.602/11.608 M/s. Each process remained internally tight
across both initialization conditions, yet the processes occupied different overall states.
Consumer empty retries were nearly zero in both groups; full start naturally gave no initial
empty observation, but did not select a persistent higher or lower rate.

The ignored `results/spsc-retry-diagnostic-20261001/phase/` directory retains the exact script,
probe source/binary hashes, sidecar, all 320 raw segment rows, global process/block order,
ordered and block-difference CSVs and SVGs, anomalous rows, qualification snapshots,
assessments, and before/fixed/restored cpufreq readbacks.

## Decision

Empty versus completely full queue at the timed boundary did not explain the observed
cross-process rate states in this diagnostic. The result does not rule out other sustained
occupancy distributions, worker phase relationships, or physical/code placement effects.
The measured process separation is much larger than the controlled initialization effect.
Further broad route or parameter comparisons would still risk treating process state as a
mechanism difference. The next research package needs a protocol that identifies or controls
the state and then demonstrates repeatable differences in multiple independent, canonical
uninstrumented benchmark processes. Busy contention remains part of the intended workload.
