# Experiment: Where does busy-retry SPSC rate dispersion arise?

- Type: measurement-method diagnostic, not a route ranking
- Status: complete for process, page-offset, ASLR, and same-process interleaving hypotheses;
  cache cause unresolved
- Source revisions: `5799b9f23cd3397ebe94c634003ce61d966df0c0` (initial probes),
  `94f6f16be06b1802ab3bd968d9d07b3874e397c1` (ASLR and route interleaving)
- Date: 2026-10-01 (Asia/Shanghai)

## Question and boundary

[Experiment 023](023-spsc-measurement-stability.md) found that dedicated-core busy-retry
8 B / 64-slot SPSC throughput varied within each route despite fixed delivered frequency and
passing host checks. Does that variation arise within a sustained row, across trials in the same
process, or mainly across process launches? Are failed queue attempts or the queue object's page
offset sufficient to explain the states? Busy retries and their contention are the intended
workload; a stable difference due to them would be a mechanism result. This diagnostic asks why
nominally repeated rows change state before estimating small cross-route differences.

## Probe and controls

A local Clang 21.1.8 `-O3 -DNDEBUG` probe reused the three actual SPSC ring classes and the
benchmark's 8-byte payload generation, per-message validation, and checksum. It pinned the
producer to CPU 1 and consumer to CPU 2; the coordinator ran on CPU 0. Each trial used 2 million
warmup and 100 million timed messages, split into ten 10-million-message segments. Each worker
counted failed `try_push` or `try_pop` calls locally and read `steady_clock` at segment boundaries.
The independent probe adds instructions on failed attempts and is not equivalent to the canonical
benchmark's timed path. Its rates are diagnostic only and must not be pooled with formal CSVs.

The clean source revision passed CI run `36818351702` (all five jobs) and the native Release
suite (168/168). All diagnostic groups saved and verified the original cpufreq policy, set all
four policies to 2400 MHz, and restored the saved policy after completion or interruption.
Each materially different probe shape had a sustained qualification with exact worker placement,
full-busy `turbostat` windows at 2400 MHz, package power/temperature, worker IRQ and runnable
thread snapshots, SMI, and thermal-throttle checks. All retained formal groups passed their
checks. The largest observed qualifying package temperature was 65 C and power 6.60 W; no
worker device IRQ, SMI, or thermal-throttle delta was found. The local `results/` directory
retains sidecars, exact scripts and probe binaries/hashes, raw segments, global order, host
snapshots, assessments, and both aborted and completed attempts.

## Observations

In the first two 12-row interleaved diagnostics, the consumer almost never found an empty queue
while the producer repeatedly found a full one. In the first version, producer failure counts
per 100-million-message basic row ranged from 0.94 to 1.59 billion; cache-line from 0.36 to
0.52 billion; cached-index from 0.38 to 0.44 billion. Removing a per-message modulus used to
detect segment boundaries changed the values and rate states. The second version still had
4-row mean-segment-rate ranges of 13.040-14.034 M/s (basic), 14.419-16.477 M/s (cache-line),
and 11.664-13.966 M/s (cached-index). Some faster rows had more failed producer attempts, so
failure count alone is not a monotonic explanation of speed. These are measurements of the
intended full-queue contention under an instrumented workload, not evidence that contention
should be removed.

A same-process probe then repeated `cached-index` with a new ring and worker pair for each trial,
reusing the same queue address. Four processes with four trials each showed within-process
ranges of 0.005-0.047 M/s, while process means ranged from 11.593 to 13.870 M/s (8.88% sample
CV). An eight-process repeat was not a monotonic time trend: process means ranged from 10.921 to
13.913 M/s and slow states recurred at processes 1, 3, and 7. Seven of those eight processes
had within-process trial ranges at most 0.084 M/s; process 6 also switched within-process
(1.453 M/s range). The state often persists over multiple trials but can change without a new
process.

All observed stack-allocated rings had a 64-byte-aligned page offset, but the page offsets
varied between launches. An explicit-placement probe then qualified offsets 0, 2048, 2688,
and 3200 bytes and ran eight processes in the symmetric order `0,2048,2688,3200,3200,2688,
2048,0`. The two process means at each *same* offset differed by 0.813-1.732 M/s. Thus fixing
the queue page offset alone did not fix the state. A further four-process probe changed all four
offsets *within* each process, alternating forward and reverse order. Each process spanned
2.152-2.912 M/s across offsets, yet each offset's four observed rates also spanned at least
2.018 M/s across processes. Placement influences the diagnostic, but no offset has a universal
fast or slow state. Relative addresses, thread stacks, cache mapping, and other process state
remain possible influences, not established causes.

At a fixed queue page offset of zero, a balanced adjacent-pair test alternated normal process
launches with `setarch -R`; every row verified the effective personality bit. The eight ASLR-on
process means had a 10.939-13.884 M/s range and 9.92% sample CV. The eight ASLR-off means still
had an 11.633-13.869 M/s range and 6.57% sample CV. Adjacent off/on differences ranged from
-16.03% to +18.28% and changed sign. Both workload shapes passed their separate 2400-MHz,
placement, IRQ, SMI, power, temperature, and throttle qualifications. Disabling ASLR did not
produce a stable process rate or isolate its cause.

A separate probe interleaved basic, cache-line, and cached-index *inside one process* in four
balanced six-row blocks, with a new ring and worker pair per row at queue page offset zero.
Each route passed its own actual-workload host qualification. Two independent 24-row processes
produced these diagnostic summaries (M/s medians and sample CVs):

| Process | Basic | Cache-line | Cached-index | Cache-line/basic block differences | Cached-index/basic block differences |
| --- | --- | --- | --- | --- | --- |
| A | 13.165 / 0.52% | 16.392 / 0.46% | 10.949 / 0.51% | +23.86% to +24.75% | -17.04% to -15.89% |
| B | 13.839 / 1.21% | 16.134 / 0.25% | 13.818 / 0.13% | +16.07% to +17.14% | -0.76% to +0.67% |

Thus interleaving reduced within-process spread, but the relative effects still changed across
independent processes, especially for cached-index/basic. The probe retained its failed-attempt
counters and ten segment clock reads per row, so even the repeatable cache-line direction here is
not a formal canonical-harness estimate. Raw segments, global orders, sidecars, qualifications,
assessment files, ordered and paired/block-difference charts, and restored-policy readbacks are in
`results/spsc-retry-diagnostic-20261001/aslr/`, `v7/`, and `v7b/`. The scripts `run-aslr.sh`,
`run-v7.sh`, and `run-v7b.sh` reproduce these diagnostic shapes. All 320 ASLR and 240 segments
per interleaved process remain available, including anomalous rows.

The canonical throughput timer reads only at phase boundaries. Together with the isolated
[clock-read probe](024-clock-read-calibration.md), these multi-second row differences cannot
plausibly be attributed to the nanosecond clock-read floor. The host checks narrow the usual
frequency, migration, device IRQ, SMI, and thermal explanations without proving that no
transient interference occurred. The probe's own instruction changes and the interaction of
address placement with queue design still limit causal attribution.

## Decision

The isolated host supports controlled observation of busy-retry behavior, but the current
one-route-per-process harness does not support precise small SPSC throughput gaps at this
shape. Neither disabling ASLR nor same-process route interleaving alone established a stable
cross-process effect. The next bounded test should control or vary relative addresses of the
queue and worker-accessed data, or validate a specific cache hypothesis with role-specific PMU
counts. Any proposed comparison protocol must then repeat across independent processes in the
canonical 8 B / 64-slot benchmark without diagnostic counters and show dispersion below the
effect of interest. No particular cache event, host fault, or queue implementation defect has
been established. Busy retries remain the intended dedicated-core condition. Do not expand to
other SPSC parameter groups or three-worker routes on the strength of these diagnostic rates.
