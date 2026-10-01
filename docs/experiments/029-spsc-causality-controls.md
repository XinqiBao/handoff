# Experiment: Are SPSC rate states generic host noise or sampler effects?

- Type: measurement-method diagnostic, not a route ranking
- Status: complete for the no-handoff and sampling controls; SPSC state cause unresolved
- Source revision: `c7a89e44d14e3f12b46d2caf9e4fe329dca3d0e2`
- Date: 2026-10-01 (Asia/Shanghai)

## Question and boundary

The [process-state](025-spsc-process-state-diagnostic.md) and
[address](027-spsc-address-state-diagnostic.md) diagnostics found different
`cached-index` throughput states across independently launched processes. Are
comparably large differences inevitable even for a simple dual-core workload
on this host? Does the address sampler itself change the queue rate? Neither
control identifies the cause of the uninstrumented SPSC states or measures
one-way message latency.

## Protocol and host gate

The clean source revision passed all five CI jobs (run `36840425344`) and its
native Release suite passed 168/168 tests. Both local Clang 21.1.8 `-O3`
probes were compiled on this host. The first ran a deterministic dependent
arithmetic/512-byte lookup on CPU 1, with a busy companion on CPU 2 and a
CPU-0 coordinator. Each of 16 independent processes retained ten timed
100-million-step segments and an identical nonzero checksum. This control
uses two dedicated cores and the same segment clock type, but has no
producer-consumer handoff, shared queue lines, or equivalent package power.

The second control reused the actual `CachedIndexBoundedRing`, 8-byte
payload generation, per-message validation, and checksum from the earlier
local probe. CPU 1 produced and CPU 2 consumed 2 million warmup plus 100
million timed messages per independent process, retaining ten timed segments
and local retry counts. The queue page offset was zero. Eight adjacent process
pairs alternated `off,on` and `on,off`. Both modes used the same binary and
root identity; `on` added CPU-2 `perf record` with precise
`mem_load_uops_retired.l2_miss` data-address sampling at period 200,000.
`off` ran the binary directly. Perf setup and sampling can both affect the
system; the paired contrast cannot isolate interrupt time from a changed
producer-consumer phase. The local probe's retry counters and segment clocks
make its rates diagnostic, not canonical benchmark throughput.

Each materially different workload saved all four original cpufreq policies,
requested and read back 2400 MHz, and restored the saved policy exactly on
exit. Under the actual workloads, the no-handoff control passed 46 full-busy
windows at 2400 MHz, 58-62 C, and 5.31-6.23 W. The SPSC `off` and `on`
qualifications passed 28 and 34 full-busy windows respectively, both at 2400
MHz, 64-68/63-65 C, and 6.18-6.30/6.12-6.43 W. Worker placement snapshots,
device IRQ, SMI, and thermal-throttle checks passed. The formal groups also
had no worker device IRQ or throttle delta. These checks narrow external
causes; they cannot establish that every nanosecond was interruption-free.

## Observations

The 16 no-handoff processes had a median of 184.183927 million steps/s,
sample CV 0.000074%, and maximum-to-minimum span 0.000303%. All checksums
matched. Thus a general inability of this host, clock, or fixed-frequency
profile to repeat a long CPU-bound process is inconsistent with this control.
It does not rule out disturbances or hardware behavior specific to shared
cache lines and cross-core handoff.

For the SPSC diagnostic, the eight `off` rows ranged from 10.957 to 13.677
million messages/s (median 12.984, sample CV 7.49%). The eight sampled `on`
rows ranged from 10.923 to 11.377 (median 10.983, CV 1.74%). Adjacent-pair
`on/off` differences had a median of -15.77%, with seven negative pairs and
one **positive** pair (+0.28%); the full range was -20.03% to +0.28%.
The sign-reversing seventh pair is retained. The sampled mode decoded
1,013 addresses across the eight formal processes. The mode difference is
large and repeats under both execution orders, supporting a sampler-associated
change in this workload. It does not imply that sampling caused the original
unsampled process states, nor that roughly one thousand recorded samples
directly consumed the missing time. The unsampled rows still occupy several
rate states.

The first-to-last segment median rate increase was 0.286% for `off` and
0.403% for `on`; deleting the first segment left the `off` median at 12.986
million/s with 7.48% CV. A short initial transient is visible in some rows,
but it does not account for the persistent cross-process gap. This does not
prove that 2 million warmup messages establish every possible sustained
phase or address state.

Ignored `results/spsc-causality-20261001/` and the earlier
`results/spsc-retry-diagnostic-20261001/probe-pfn.cpp` retain the local probe
sources, binaries and hashes, exact scripts, sidecars, all 320 raw formal segments,
global process order, sampler perf binaries and decoded addresses, qualification
and formal host snapshots, original/fixed/restored policies, assessments,
and ordered/paired SVGs. No row was excluded. `sampling-pairs/paired-differences.csv`
and `ordered-metrics.csv` are generated from the retained per-process CSVs.

## Interpretation and next gate

The current canonical **throughput** metric counts completed, validated
handoffs over the whole timed phase; it uses two boundary clock reads per
trial, not a per-message timestamp or random sample. A clock-read floor of
roughly 28 ns cannot plausibly make multi-second rows differ by 7-20%.
Canonical ping-pong measures a *round trip* with two clock reads per exchange
and retains only trial median/p95/p99 in CSV. It neither establishes one-way
latency nor preserves its individual RTT samples for tail reanalysis. A
fine-latency campaign should first retain ordered raw RTT samples outside
the hot path and test timing-method perturbation end to end.

The earlier consumer `l2_request.miss` association counts L2 accesses that
miss, whereas the sampled event counts *retired load uops* that miss L2 and
can supply a data address. The event populations differ. Neither identifies
why a line missed or whether misses caused the slower process. In particular,
the ring's 512-byte slot array and following 64-byte index line are small;
an L2 miss is not evidence that its capacity-sized working set exceeds L2.
Brendan Gregg's [WSS discussion](https://www.brendangregg.com/wss.html)
notes that PMC hit rates depend on access distribution, associativity, and
multicore behavior, and that page-based WSS tools operate at a different
granularity and perturb execution. His
[active benchmarking discussion](https://www.brendangregg.com/activebenchmarking.html)
also warns that a statistically tidy benchmark can still measure the wrong
limiter. Neither article supplies a diagnosis for this queue.

The next discriminating experiment should vary one explicit shared-line or
worker-address relationship while keeping the same workload, then require
multiple independent **canonical, unsampled** processes to repeat below the
effect of interest. An isolated cache-line layout change is a hypothesis, not
an established fix: the current cached-index design intentionally places
both workers' counters on one line, and changing it would also change the
mechanism being compared. Until that gate passes, the small 8 B / 64-slot
SPSC gaps and an expanded parameter/fan-out matrix have no reliable
fine-grained ranking interpretation. Busy-retry contention remains part of
the intended dedicated-core behavior.
