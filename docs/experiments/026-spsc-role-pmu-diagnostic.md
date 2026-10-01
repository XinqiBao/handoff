# Experiment: Do SPSC process rate states track worker cache events?

- Type: measurement-method diagnostic, not a route ranking or source-level attribution
- Status: complete for the selected PMU association; address cause unresolved
- Source revision: `6496d3e834cf3dd6b8cc3ebe47c255be3af41caa`
- Date: 2026-10-01 (Asia/Shanghai)

## Question and boundary

[Experiment 025](025-spsc-process-state-diagnostic.md) found different rate states across
otherwise qualified processes running `cached-index` with dedicated busy-retry workers. Does the
slower state coincide with a role-specific cache-event difference under the same 8 B / 64-slot
workload? An association would guide a narrower placement or cache experiment; it would not
identify a physical cache set, a source line, or whether misses cause or follow a changed retry
state.

## Protocol

The clean, CI-green revision above passed all five CI jobs (run `36831612157`) and the local
native Release suite (168/168). The local Clang 21.1.8 `-O3 -DNDEBUG` probe from Experiment 025
used the actual `CachedIndexBoundedRing`, 2 million warmup and 100 million timed validated
messages per process, with ten 10-million-message raw segments and local failed-attempt counts.
It pinned the producer to CPU 1 and consumer to CPU 2; the coordinator stayed on CPU 0. The
queue's page offset was zero and normal ASLR remained enabled. Each of 16 independent process
launches ran one row. CPU-wide `perf stat -A -C 1,2` counted cycles, instructions,
`l2_request.miss`, `mem_load_uops_retired.hitm`, and `cache-misses` separately for the two
worker cores. All selected event rows reported 100% counter running time. CPU-wide counts include
any other activity on those cores and cover process setup/teardown as well as timing; they are
diagnostic, not equivalent to instrumenting only the timed region.

The session saved all four cpufreq policies, requested and read back 2400 MHz, and restored the
exact saved policies afterward. A sustained four-row probe under the same PMU collection passed
31 full-busy `turbostat` windows: each worker delivered 2400 MHz, package temperature was
62-64 C and power 6.17-6.41 W, with no worker device IRQ, SMI, or thermal-throttle delta.
Mid/late snapshots found only the two expected runnable workers on CPUs 1 and 2. `turbostat`
printed an initial slow-TSC diagnostic warning; its active APERF/MPERF samples passed the gate.
This warning was recorded in the sidecar. The formal group's before/after
device IRQ and throttle counters also did not change on worker cores.

## Observations

The 16 process rates ranged from 10.931 to 13.891 million messages/s, median 12.938 million/s,
sample CV 9.57%. Several rates clustered near 10.93, 11.64, 12.94, and 13.88 million/s rather
than following one monotonic run-order trend. The consumer retired nearly the same instruction
count in every process (sample CV 0.0031%) and almost never needed an empty retry. The producer
made many full retries, but retry count alone does not isolate the state.

The four slowest rows averaged 10.935 million/s; the four fastest averaged 13.885 million/s.
Consumer `l2_request.miss` averaged 133.02 million events in the slow four versus 101.49 million
in the fast four, 31% higher for the same message count. Producer counts were 58% higher in the
slow four. Across all 16 rows, the descriptive Pearson correlation between rate and consumer
L2 misses was -0.989; producer L2 misses correlated at -0.976. These correlations are not
independent estimates of a causal effect. `mem_load_uops_retired.hitm` did not show the same
simple monotonic association (consumer rate correlation +0.418); the data do not establish a
single coherence event as the explanation. Generic `cache-misses` counts were much smaller than
the model-specific L2 event and should not be treated as an interchangeable measure.

Working output was collected under ignored `results/spsc-retry-diagnostic-20261001/pmu/`.
Analysis included all 160 segment rows and each per-core PMU row; no anomalous row was removed.

## Decision

The rate states coincide with a large change in per-message L2 misses under nearly identical
consumer instruction work. This makes address/cache behavior a concrete next target and further
weakens a clock-read explanation. The event definition counts core L2 requests that miss; it
does not name the accessed line or distinguish a change in mapping from changed occupancy and
coherence traffic. Next inspect queue and worker-accessed address relationships or collect
address-resolved samples under a protocol that distinguishes these hypotheses. Then confirm any
stabilization in multiple independent processes using the canonical, uninstrumented benchmark.
The present diagnostic does not authorize a broad SPSC or fan-out performance ranking.
