# Experiment Records

Numbers preserve research identity and chronology; they are not prerequisites or a recommended
reading sequence. Use the [mechanism catalog](../mechanisms/README.md) for conceptual paths and
source/test navigation. The records below preserve exact revisions, procedures, negative results,
and limits.

Experiments begin with a question or hypothesis, not an unbounded collection of numbers. Each
mechanism result identifies itself as a mechanism-isolation experiment or an implementation
comparison. Host calibration may instead be recorded as measurement-method characterization. All
follow the repository [benchmark methodology](../benchmark-methodology.md).

Use [`template.md`](template.md) for a new experiment. Keep setup and results specific to the
question, preserve individual trials where practical, and separate observations from explanations.
Do not commit routine smoke timings as experimental evidence.
Create a planned record only when a question has a defensible protocol and is promoted for
execution. Earlier unmeasured plans remain in Git history, not in the active experiment index.
One record may combine a deterministic diagnostic, canonical comparison, and targeted sensitivity
checks when they answer one coherent question; a command or benchmark mode alone does not require
its own record. Keep unrelated research questions separate. A new anomaly investigation needs a
specific hypothesis, a discriminating protocol, and an explanation of how either outcome changes
a useful conclusion or engineering decision. Add follow-up controls to the same record when they
answer that question rather than numbering every probe.

For current practice, start with the [methodology](../benchmark-methodology.md),
[host guide](../measurement-host.md), and
[current comparison boundary](../roadmap.md#controlled-performance-comparisons-current-boundary).
A record's proposed next gate belongs to its stated revision; later campaign decisions may supersede
it without rewriting the evidence. Raw output is local and ignored, not bundled with this index.
Each record names its evidence directory and procedures; retain those scripts, source/binary
identities, raw order, host checks, and interrupted attempts together.

## Measurement reference records

- [031: Busy-retry MPSC completion comparison](031-busy-retry-mpsc-comparison.md):
  the current canonical workload retains a resolved complete-route direction.
- [022: Fixed-frequency MPSC completion comparison](022-fixed-frequency-mpsc-comparison.md):
  a resolved complete-route direction under historical yield retries.
- [023: Scalar SPSC measurement stability](023-spsc-measurement-stability.md):
  workload-dependent resolution and the transition to busy retries.
- [030: Ordered SPSC RTT samples and process repeatability](030-spsc-rtt-repeatability.md):
  raw-output control and a bounded two-ring tail comparison.

The intermediate SPSC controls remain under [supporting investigations](#supporting-measurement-investigations).
These reading aids do not promote old workloads into current rankings.

## Single-producer baselines and dependencies

- [Basic versus cache-line-separated SPSC](003-cache-line-spsc-comparison.md)
- [Basic versus cached-index SPSC](004-cached-index-spsc-comparison.md)
- [Basic scalar versus all-or-nothing batch SPSC](005-batch-spsc-comparison.md)
- [Bulk versus staged direct-slot SPSC](007-staged-spsc-comparison.md)
- [Head/tail SPSC versus sequence publication](008-sequence-publication-comparison.md)
- [Cost of reliable sequence fan-out](009-sequence-fan-out-comparison.md)
- [Cost of a fixed sequence dependency](010-sequence-dependency-comparison.md)

## Lossy observation

- [Lossy sequence-payload behavior under offered load](014-sequence-payload-offered-load.md)

## Shared claims and topology

- [Serialized ownership versus ordered MPSC publication](015-mpsc-ordered-publication.md)
- [Independent MPSC completion and FIFO visibility](016-mpsc-producer-completion.md)
- [Consumer coordination and work sharing](017-spmc-consumer-coordination.md)
- [Ordered worker-stage progress](018-ordered-worker-stage.md)
- [Producer-owned paths and merge authority](019-two-path-merge.md)
- [Independent branch writes at a fixed join](020-two-branch-join.md)
- [Paired MPSC descriptor and byte credit](021-mpsc-variable-record.md)

## Supporting measurement investigations

These preserve the path to the current boundary, including failed stabilization hypotheses and
instrumentation effects. Read them when checking a specific explanation or reproducing a probe;
the unresolved SPSC cause is not a global gate for other workloads.

- [Linux measurement host baseline](linux-host-baseline.md)
- [Clock-read floor on the isolated N150](024-clock-read-calibration.md)
- [Busy-retry SPSC process-state diagnostic](025-spsc-process-state-diagnostic.md)
- [Role-specific PMU check of SPSC rate states](026-spsc-role-pmu-diagnostic.md)
- [Physical pages and sampled addresses in SPSC rate states](027-spsc-address-state-diagnostic.md)
- [Initial queue occupancy and SPSC process states](028-spsc-initial-occupancy-diagnostic.md)
- [No-handoff, sampler, and layout controls for SPSC rate states](029-spsc-causality-controls.md)
