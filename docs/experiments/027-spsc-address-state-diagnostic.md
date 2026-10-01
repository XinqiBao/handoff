# Experiment: Do SPSC rate states follow page placement or sampled addresses?

- Type: measurement-method diagnostic, not a route ranking
- Status: complete for simple page-residue and sampled-address hypotheses; cause unresolved
- Source revision: `24643421be721745323155b5aa47faad80d02110`
- Date: 2026-10-01 (Asia/Shanghai)

## Question and boundary

[Experiment 026](026-spsc-role-pmu-diagnostic.md) associated slower independent
`cached-index` processes with more L2 request misses per completed message, without locating
the memory access. Does the queue's physical page placement determine the rate state? Do
address-resolved consumer L2 load-miss samples identify a particular area of the queue?
Neither probe changes the canonical benchmark or establishes a performance comparison.

## Protocol and controls

The clean revision above passed all five CI jobs (run `36834021855`); the unchanged native
Release code had passed 168/168 tests. A local Clang 21.1.8 `-O3 -DNDEBUG` variant of the
Experiment 025 probe logged the virtual address and privileged `/proc/self/pagemap` PFN of the
queue, a main-thread checksum and segment arrays, and each worker's warmup payload local.
These reads happened before timing. The producer stayed on CPU 1, consumer on CPU 2, coordinator
on CPU 0. Each independent process handled 2 million warmup and 100 million timed validated
8-byte messages through 64 slots in ten retained segments. The queue page offset was zero;
normal ASLR remained enabled.

The first group ran 24 independent processes without PMU sampling. It saved the four original
cpufreq policies, requested and verified 2400 MHz, and restored each original value after the
session. Its actual-workload qualification passed 27 full-busy windows at 2400 MHz, 60-63 C,
6.16-6.22 W, with exact worker placement and no worker device IRQ, SMI, or thermal-throttle
delta. The formal group's IRQ and throttle checks also passed.

A second group sampled CPU 2's `mem_load_uops_retired.l2_miss:pp` every 200,000 events with
`perf record -d`, retaining each binary trace and decoded data address. It again saved, fixed,
qualified, and restored cpufreq. Its separate actual-workload qualification passed 32 full-busy
windows at 2400 MHz, 60-63 C, 6.17-6.22 W, with the same IRQ, SMI, and throttle checks.
Sampling generates PMU interrupts on the consumer core and can change the rate state. Both
groups ran the probe as root only to access PFNs and PMU events; this is another difference
from the canonical benchmark.

## Observations

The unsampled PFN group still ranged from 10.936 to 13.682 million messages/s (median 12.938,
sample CV 8.90%). The low five bits of the queue PFN were not a sufficient state label: three
rows with queue PFN residue 11 modulo 32 ran at 13.659, 11.958, and 10.936 million/s; two
with residue 23 ran at 10.947 and 12.971 million/s. This does not rule out physical placement
effects involving other pages, higher address bits, or a cache-index function. PFNs were read
before timing, not continuously while the workers ran.

The sampled group produced 2,114 decoded consumer L2 load-miss data addresses across 16
independent rows. Every sampled address fell inside the current row's queue: 888 within the
512-byte slot array and 1,226 within the following 64-byte line that holds the index state.
No sample fell in the logged worker payload locals or elsewhere. The slowest four sampled rows
averaged 10.910 million/s with 46.1% of their samples in the index line; the fastest four
averaged 11.925 million/s with 71.7% there. These are **shares of sampled retired load misses**,
not exact counts of all L2 requests or proof that one area caused a rate state. The sampled
group's rate range was only 10.893-12.141 million/s (median 11.006, sample CV 4.03%), unlike
the unsampled and CPU-wide counting groups. Sampling perturbed the workload, so its rates and
shares must not be pooled with them or used to rank mechanisms.

The ignored `results/spsc-retry-diagnostic-20261001/pfn/` and `address-samples/` directories
retain exact scripts, probe source/binary hashes, sidecars, all 240 and 160 raw segments,
respectively, global order, virtual addresses and PFNs, per-row binary PMU traces and decoded
addresses for the sampled group, assessments, order plots, qualification snapshots, and exact
before/fixed/restored policy readbacks. No anomalous row was removed.

## Decision

The queue's simple physical-page residue does not determine its process rate. Address samples
confirm that the consumer's sampled L2 load misses concern queue storage and the index line,
but the sampler changes the observed operating state. The prior CPU-wide L2 request association
remains valid as a diagnostic; these data do not isolate a cache mapping, false sharing cost,
or implementation defect. A next useful test should change one explicit initial occupancy or
worker phase condition within one process and repeat it across processes, or use lower-impact
event collection to validate a precise placement hypothesis. Formal uninstrumented throughput
and RTT comparisons still require cross-process repeatability below the effect of interest.
