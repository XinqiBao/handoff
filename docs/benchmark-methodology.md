# Benchmark Methodology

## Measurement goals

Benchmarks should explain conditional behavior, not produce a universal queue ranking. Every
recorded experiment begins with a question or hypothesis. Mechanism results identify whether they
isolate a design property or compare representative implementations; host calibration is identified
separately and does not imply a mechanism ranking.

Correctness is a prerequisite for performance comparison. Smoke commands verify plumbing only and
must not be presented as performance evidence.

`handoff-bench run <route>` is a discovery convenience. Its defaults (8 B payload, 64 slots,
4096 bytes, or both capacities as appropriate, 100 warmup, 10000 iterations, one trial) are small
exploratory settings, not a canonical comparison protocol. The selected route's supported workloads
and native capacity kind are shown by `handoff-bench describe <route>`. `publication-hole` remains
an untimed semantic diagnostic. Record explicit workload-first options, placement, exact revision,
and the procedure below for any comparative claim. Mechanism-first and workload-first runs use the
same workload implementations and CSV field meanings.

## Evidence and claim strength

- **Semantic evidence** is a deterministic observation of an ownership, visibility, completion,
  dependency, or reuse contract under a controlled interleaving. It does not measure rate.
- **Correctness and stress evidence** covers tested histories and tool executions. It supports a
  C++ memory-order argument but does not prove all executions correct.
- **Exploratory performance observation** is a recorded rate or latency with its revision, host,
  workload, placement, and procedure. It can motivate a question, but does not establish an
  isolated cost or stable effect size.
- **Controlled performance evidence** adds repeatability and relevant confounder controls adequate
  for the specific comparative claim. It remains conditional on the tested host and workload.

An experiment may contain several of these evidence types. State which observation supports each
conclusion; do not promote a complete-route rate difference into a claim about an individual atomic,
cache line, or coherence event without an experiment that isolates it.

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

## Analysis and presentation

Keep the ordered trial rows and experiment sidecar as the source of every summary or chart. For a
comparison, show individual results in execution order, the block or pairing structure, the median
and spread for each route, and paired differences when runs were interleaved. Label workload, units,
role placement, payload, native capacity, revision, and host profile so a plotted rate is not
mistaken for a universal property. Show drift and anomalous rows rather than hiding them behind a
pooled median. Generate figures from retained raw results; a chart is an inspection aid, not an
additional measurement or a substitute for a stated comparison question.

Put routes on a common comparative axis only when their numerator, timed boundary, role work,
delivery contract, and capacity interpretation make that axis meaningful. Throughput, ping-pong
RTT, offered/observed rates, and overwrite shares need separate scales and interpretations.
Different contracts may still be compared explicitly as complete systems, with the changed work
and semantics stated; their rate difference must not be called an isolated mechanism cost.

## Throughput

Report completed messages per second from a complete producer-to-consumer handoff, not producer
publication alone, unless publication rate is the explicit subject. Validate the final count and a
minimal checksum so consumer work and payload reads remain observable to the optimizer.

Keep equivalent benchmark-side work, payload generation, validation, and termination conditions
consistent across implementations.

The two-producer `mpsc-serialized` and `mpsc-ordered` throughput modes compare complete
implementations with one consumer and fixed inline payloads. Both generate each payload from its
assigned FIFO position and validate every consumer completion in position order. The serialized
route holds a producer mutex through position assignment, payload generation, full retries, and
`BasicBoundedRing::try_push`. The ordered route claims a position with CAS, generates into its
slot, and waits for its turn at one publication frontier. Each producer performs a fixed half of
warmup and timed messages, with any odd remainder assigned to producer 0. Timed completion is the
consumer's final validated handoff; the same payload generator and checksum are used in both
routes. `--producer-cpus P0,P1 --consumer-cpu C` is an all-or-none three-role placement request,
and metadata records both producer roles. Mutex admission, reservation retries, direct slot
access, publication waiting, and cache traffic all differ. This is an implementation comparison,
not an isolated frontier cost.

`mpsc-count` and `mpsc-slot` use the same two-producer payload work, scalar claims,
FIFO validation, completed-handoff numerator, placement, and yield retries. The
count route adds one shared completion RMW per publication and occasional group-tail
CAS; the slot route release-marks one generation tag and makes the consumer check
that tag. These are complete-route comparisons with different progress semantics,
not isolated instruction costs.

The `spmc-serialized`, `spmc-ordered`, and `spmc-slot` throughput routes use
one producer and two competing workers. Each publication has one owner. All
three use identical position-derived bytes, scalar direct-slot access, yield
retries, a per-position seen count, and per-worker acquisition counts. The
producer stops timing only after every release and its verification of the
final reusable prefix. This includes reclamation work in the complete-route
rate. The serialized mutex, ordered release cursor, and generation-tagged
completion differ in progress semantics and coherence costs; rates do not
isolate one instruction. Fan-out rates count two deliveries per publication
and are not an equivalent work-sharing control.

## Publication-hole diagnostic

`publication-hole` is a bounded progress workload for MPSC publication, not a rate or latency
benchmark. It has no warmup, timed phase, or CPU placement. P0 claims position zero and waits
before writing. P1 claims each of the remaining `Capacity - 1` positions, completes their
payloads, and calls nonblocking `try_publish()` once for each claim before reporting readiness.
The coordinator checks that one more claim fails and that the consumer sees no position. At this
controlled snapshot, `Capacity` reservations and `Capacity - 1` payloads are complete, all
`Capacity - 1` later publication attempts have been rejected, zero blocking publication calls
have returned, zero positions are visible, and zero consumer completions have occurred. The
probe establishes that the hole rejects later publication; it does not time scheduler entry into
the following wait loop. No shared diagnostic counter is added to the mechanism hot path.

The same command accepts `--implementation mpsc-count|mpsc-slot`. In those routes,
the later owner publishes every remaining claim and all `Capacity - 1` calls return
before the first owner is released. Rejected attempts are zero; no position is
visible and another claim fails. The count and slot routes differ after an early
hole closes while a newer claim remains unfinished; deterministic mechanism tests
cover that group-tail lag and consumer prefix discovery separately.

The coordinator then allows P0 to write and publish. In the ordered route, P1
publishes its held positions in order; in the independent-completion routes P1
already returned. The consumer validates and releases all `Capacity`
messages. The final count and checksum establish recovery after the deliberate
delay. This workload demonstrates bounded admission and ordered progress, but
does not measure stall duration, fairness, CPU use, or throughput. Its CSV has
a separate progress schema so those counts cannot be mistaken for rates.

## Ping-pong latency

Collect repeated round-trip time samples and report their distribution. A value derived as RTT/2 is
only a proxy under symmetry assumptions; it is not an exact one-way latency measurement. Avoid
mixing queueing latency from an offered-load test into the minimum-ish ping-pong interpretation.
The default `--wait yield` calls the scheduler on an unavailable request or response. For a
minimum-ish dedicated-core comparison, `--wait spin` retries immediately without that scheduler
call. Record the selected policy and compare only rows using the same policy; it changes both CPU
use and measured latency. The timed request/response work, validation, and RTT clock reads are
otherwise the same. Neither mode measures offered-load queueing latency.

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
Linux host isolation and delivered-frequency qualification are described in the
[measurement host guide](measurement-host.md); a verified affinity request alone
does not establish exclusive CPU use.

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
