# Benchmark Methodology

## Measurement goals

Benchmarks should explain conditional behavior, not produce a universal queue ranking. Every
recorded experiment begins with a question or hypothesis. Mechanism results identify whether they
isolate a design property or compare representative implementations; host calibration is identified
separately and does not imply a mechanism ranking.

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

Harness phase and completion waits use portable atomic blocking notifications so the coordinator
does not consume a worker or housekeeping core during the timed phase. This control-plane choice
does not change the current mechanism-side `yield` loops used when publication or observation is
temporarily unavailable.

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
unless that placement is the experiment. After applying a Linux request, read back the current
thread mask and require it to contain exactly the requested CPU before reporting effective
placement. If affinity is unsupported, report that fact and continue only when the remaining
measurement is meaningful.

NUMA topology discovery is not part of the core harness.

## Result output

Human-readable stdout should remain concise. CSV is the structured result format. The initial schema
is expected to include, where meaningful:

```text
benchmark, implementation, payload_bytes, capacity_slots, capacity_bytes,
batch_size, iterations, trial, elapsed_ns, messages_per_second, latency_ns,
latency_p95_ns, latency_p99_ns, checksum, producer_interval_ns,
consumer_stall_every, consumer_stall_ns, offered_messages, observed_messages,
overwritten_messages, retry_attempts, observed_payload_bytes,
offered_messages_per_second, observed_messages_per_second
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
checksum before a result is emitted. CSV records `consumer_count=2` as metadata. Controlled
placement uses `--producer-cpu P --consumer-cpus C0,C1`; list order maps directly to consumer
indices. Metadata records requested and verified effective CPUs plus an outcome for each role.
Partial, duplicate, or producer-overlapping placement is rejected.

The `pipeline` throughput implementation uses one producer followed by fixed upstream and
downstream consumer stages. A completed message means both stages have performed the same payload
validation and the downstream stage has released the publication. `iterations` and rate count
these end-to-end completions, and both independent checksums must match before a result is emitted.
CSV records `consumer_count=2`. The same fixed placement interface maps consumer list order to the
upstream and downstream roles and records each role independently. The pipeline is not offered as
a scalar ping-pong mode.

The `fixed-record` implementation keeps the selected `payload_bytes` as an inline payload but also
copies and validates a 16-byte record header containing sequence, type tag, and logical payload
length. It uses scalar copy-in/copy-out operations in throughput and ping-pong. Its
`capacity_slots` field remains the exact number of usable records, while `capacity_bytes` stays
empty because neither payload bytes times slots nor logical record bytes describe the complete ring
object footprint. Header work is part of this mechanism's contract and timed workload.

The `byte-record` implementation copies the same logical header and payload into contiguous
16-byte-aligned footprints in a circular byte buffer. It parses and skips explicit padding headers
at physical wrap. Its native capacity is bytes: CSV populates `capacity_bytes` and leaves
`capacity_slots` empty. Alignment, padding, parsing, and complete header/payload copies are part of
the timed contract. A byte capacity must not be relabeled as an equivalent slot count.

The `descriptor-record` implementation stores each logical header and payload location in a fixed
descriptor slot while copying payload bytes into a separate aligned byte ring. Descriptor access,
payload copying, and any physical end gap are timed work. Both capacities are native constraints,
so CSV populates `capacity_slots` and `capacity_bytes`; neither may be converted into the other.
Supported benchmark pairs are 64 descriptors with 4096 payload bytes and 1024 descriptors with
65536 payload bytes. The pairing limits the experiment matrix; it does not claim equal effective
message capacity across payload sizes or storage layouts.

The `sequence-payload` implementation appears only in `offered-load`. `iterations` is the fixed
number of timed publications offered by the producer. A zero producer interval is unpaced;
positive intervals place publications on successive absolute `steady_clock` deadlines so scheduler
delay does not accumulate through repeated relative sleeps. A consumer stall occurs after every N
successful timed observations and is disabled only when both the interval and duration are zero.
The same shape applies during untimed warmup, which is fully accounted before the timed phase.

The consumer owns its requested sequence. After `overwritten`, it selects
`max(requested + 1, available_range.oldest)`, capped just past the phase's final offered sequence,
and counts the exact selected distance as overwritten. It retries unstable snapshots without
advancing. After the producer finishes, the observer drains until every timed offer is either
observed or overwritten, enforcing `offered_messages = observed_messages + overwritten_messages`.
The checksum covers only payloads actually observed, and `observed_payload_bytes` sums their
logical lengths.

`elapsed_ns` spans the complete timed observation window, from phase release through final drain.
Both offered and observed rates use that same denominator. The generic `messages_per_second` and
all latency fields remain empty because producer publication completion is not separately timed and
the workload does not collect per-publication latency. Pacing and stall dimensions and all raw
accounting fields are present only on offered-load rows; they remain empty elsewhere.

For ping-pong, `latency_ns` is the per-trial median measured RTT. The p95 and p99 columns are also
RTT values. Throughput leaves all latency columns empty. If a clock reports a zero elapsed duration,
the corresponding rate is unavailable and remains empty rather than being synthesized from a
one-nanosecond denominator.

Lightweight run metadata should accompany results as CSV comments or a simple adjacent file. Record
at least git revision and dirty state, compiler and version, build mode, OS, CPU model, requested and
effective CPUs, affinity outcomes, mechanism and control waiting behavior, warmup, and trial count.
Facts that can change
after configuration, including Git state, must be collected when the command starts.

## Interpretation and CI

Report observations separately from causal explanations and list important confounders. Do not infer
algorithmic effects from differences near the observed noise floor.

GitHub-hosted CI may configure, build, test, run sanitizers, and execute small benchmark smoke tests.
Hosted-runner timings are not performance regression data and must not support performance claims.
