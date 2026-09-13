# Benchmark Methodology

## Measurement goals

Benchmarks should explain conditional behavior, not produce a universal queue ranking. Every
recorded experiment begins with a question or hypothesis and identifies whether it isolates a
mechanism property or compares representative implementations.

Correctness is a prerequisite for performance comparison. Smoke commands verify plumbing only and
must not be presented as performance evidence.

## Workload categories

- **Steady-state throughput** measures sustained handoff over a sufficiently long timed run.
- **Ping-pong round-trip latency** repeatedly sends work and waits for an acknowledgement to
  approximate minimum-ish handoff latency.
- **Burst throughput** studies batched offered work and drain behavior.
- **Imbalance and stall workloads** study backpressure, occupancy, and recovery when producer and
  consumer rates differ.
- **Offered-load latency** studies queueing latency as load approaches saturation.

Minimum-ish handoff latency, queueing latency under load, and throughput saturation answer different
questions and should not be conflated.

## Clock and timed region

Use `std::chrono::steady_clock` initially. Throughput runs should be long enough that timer overhead
is negligible. Architecture-specific cycle counters require a demonstrated need and separate clock
validation before adoption.

Unless explicitly under study, the timed region excludes:

- allocation and queue construction;
- thread creation and teardown;
- CPU pinning and workload initialization;
- result validation beyond the minimal in-loop consumer work;
- result formatting and file output.

Threads should synchronize immediately before a timed phase so startup skew is not measured. Exact
phase boundaries belong in each workload's mechanism or experiment note.

## Warmup and trials

Use an untimed warmup appropriate to the workload, then run multiple independent trials. Preserve
individual trial results when practical and use the median as the default stable summary. Record the
warmup, trial count, and iteration count.

Do not silently discard outliers. If a run is invalidated by an observed external event, retain the
raw record when practical and document the exclusion. Separate unexplained measurement noise from
effects attributable to the mechanism.

## Throughput

Report completed messages per second from a complete producer-to-consumer handoff, not producer
publication alone, unless publication rate is the explicit subject. Validate the final count and a
minimal checksum so consumer work and payload reads remain observable to the optimizer.

Keep equivalent benchmark-side work, payload generation, validation, and termination conditions
consistent across implementations.

## Ping-pong latency

Collect repeated round-trip time samples and report their distribution. A value derived as RTT/2 is
only a proxy under symmetry assumptions; it is not an exact one-way latency measurement. Avoid
mixing queueing latency from an offered-load test into the minimum-ish ping-pong interpretation.

## Correctness gates

The canonical [testing strategy](testing-strategy.md) defines common invariants, mechanism-specific
checks, payload contracts, and tool roles. Sanitizers and passing executions support but do not
prove a concurrency algorithm's memory-model argument.

## CPU placement

Serious Linux runs should choose explicit producer and consumer CPUs and avoid crossing NUMA nodes
unless that placement is the experiment. Record requested and effective placement. If affinity is
unsupported, report that fact and continue only when the remaining measurement is meaningful.

NUMA topology discovery is not part of the core harness.

## Result output

Human-readable stdout should remain concise. CSV is the structured result format. The initial schema
is expected to include, where meaningful:

```text
benchmark, implementation, payload_bytes, capacity_slots, capacity_bytes,
batch_size, iterations, trial, elapsed_ns, messages_per_second, latency_ns,
latency_p95_ns, latency_p99_ns, checksum
```

Fields that do not apply remain empty rather than receiving misleading sentinel values. Schema
changes must be deliberate because historical data may depend on them.

`capacity_slots` is the native capacity for fixed-slot rings. Their `capacity_bytes` field remains
empty: payload bytes times slots describes neither the complete object footprint nor a native byte
capacity. Byte-oriented mechanisms may populate that field when they define capacity in bytes.

For throughput, `batch_size` is the number of messages requested per workload group. The scalar
`basic` baseline still publishes each message separately; the `batch` and `bulk` modes publish or
release the complete group with one counter update. The `burst` mode may complete a prefix and then
retries the remaining suffix, using returned counts so that iterations and rate still describe
completed messages. The `staged` mode reserves the complete group, writes and reads through physical
ring spans, and explicitly finishes once per side. Iterations and warmup must be divisible by the
selected batch size. Ping-pong leaves the field empty because it remains a scalar request/response
exchange.

The `sequence` implementation remains scalar in both workloads. Throughput generates into a
one-element claim and observes through a const token before release. Ping-pong transfers local
request and response values through one claim and observation at a time. Its completed counts,
payload validation, phase boundaries, yield waiting, and CSV fields have the same meaning as the
other scalar implementations.

The `fan-out` throughput implementation uses one producer and two reliable consumers. A completed
message means one publication has been observed, validated, and released by both consumers;
`iterations` and rate count these completed publications rather than summing consumer deliveries.
Both consumers perform the same payload validation and must independently produce the expected
checksum before a result is emitted. CSV records `consumer_count=2` as metadata. Because the current
affinity interface names only one consumer, `fan-out` rejects both CPU affinity options instead of
recording an incomplete placement; its smoke runs are not controlled performance evidence.

The `pipeline` throughput implementation uses one producer followed by fixed upstream and
downstream consumer stages. A completed message means both stages have performed the same payload
validation and the downstream stage has released the publication. `iterations` and rate count
these end-to-end completions, and both independent checksums must match before a result is emitted.
CSV records `consumer_count=2`; the current single-consumer affinity interface is rejected for the
same metadata reason as fan-out. The pipeline is not offered as a scalar ping-pong mode.

For ping-pong, `latency_ns` is the per-trial median measured RTT. The p95 and p99 columns are also
RTT values. Throughput leaves all latency columns empty. If a clock reports a zero elapsed duration,
the corresponding rate is unavailable and remains empty rather than being synthesized from a
one-nanosecond denominator.

Lightweight run metadata should accompany results as CSV comments or a simple adjacent file. Record
at least git revision and dirty state, compiler and version, build mode, OS, CPU model, requested and
effective CPUs, affinity outcomes, waiting behavior, warmup, and trial count. Facts that can change
after configuration, including Git state, must be collected when the command starts.

## Interpretation and CI

Report observations separately from causal explanations and list important confounders. Do not infer
algorithmic effects from differences near the observed noise floor.

GitHub-hosted CI may configure, build, test, run sanitizers, and execute small benchmark smoke tests.
Hosted-runner timings are not performance regression data and must not support performance claims.
