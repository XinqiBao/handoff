# Research Direction

This document owns the current boundary, open questions, and structural observations that affect
future decisions. The [design space](design-space.md) owns vocabulary,
[mechanism notes](mechanisms/README.md) own current contracts, and
[experiment records](experiments/README.md) own historical methods, observations, and interpretation.
Completed program chronology belongs in those records and Git, rather than another execution ledger.

## Current boundary

The correctness and repository-consolidation campaign is bounded by existing mechanisms and their
supporting scaffold. It establishes publication bounds, counter lifetime and slot mapping, honest
measurement identity, and clear knowledge ownership. It does not begin another performance study,
new topology, recovery protocol, or mechanism merely to complete the taxonomy.

Wrapping fixed-slot SPSC and assignment-based record/descriptor rings require power-of-two slot
capacities so machine-counter rollover preserves physical progression. This does not impose that
restriction on finite-position mechanisms or on independently constrained byte storage. SPMC
acquisition must prove the claim position is strictly below an acquired publication bound; an
independent newer claim cursor does not establish payload visibility. Current notes and tests own
these contracts. Measurements at earlier revisions remain historical observations, without an
automatic claim about the repaired implementation.

## Established ground

The collection distinguishes scalar, layout, grouped, and staged SPSC; sequence publication,
reliable fan-out, and fixed dependencies; inline records, byte rings, and descriptor/payload storage;
lossy sequence-addressed observation; shared MPSC/SPMC completion; an ordered worker stage;
producer-owned merge; and a fixed write/join. These are locally understandable mechanisms and small
explicit compositions, not a universal queue or lifecycle framework.

The completed producer, consumer, and worker-stage studies separate out-of-order completion from a
contiguous visibility or safe-reuse frontier. The topology studies separate common claim-order FIFO
from consumer-owned merge order and independent branch publication into one join. Paired MPSC
variable-record admission adds a distinct descriptor/byte lifetime invariant. Their evidence and
skipped alternatives are recorded in [016–021](experiments/README.md#shared-claims-and-topology).
Another frontier representation or topology needs a new invariant, rather than a vacant catalog cell.

## Controlled performance comparisons: current boundary

The dedicated N150 can resolve some complete-workload directions after actual-route qualification;
it has no universal resolution or route ranking. [Experiment 022](experiments/022-fixed-frequency-mpsc-comparison.md)
observed slot availability above completion count with historical yield retries.
[Experiment 031](experiments/031-busy-retry-mpsc-comparison.md) independently resolved the same direction
under busy retries at its recorded revision. Varying paired magnitudes do not identify an isolated
publication cost or an enduring percentage. Do not pool those waiting policies or rerun merely to
replace historical numbers.

[Experiment 023](experiments/023-spsc-measurement-stability.md) establishes an important limit at
8 B / 64-slot scalar throughput: persistent, sometimes switching process rate states exceed small
route gaps despite relevant host checks. The
[supporting controls](experiments/README.md#supporting-measurement-investigations) did not establish
a clock, simple placement, initial-occupancy, or index-line stabilization cause. Instrumentation
itself can change the state. Earlier [003](experiments/003-cache-line-spsc-comparison.md) and
[004](experiments/004-cached-index-spsc-comparison.md) observations belong to their original workload,
revision, and host conditions; they are not current rankings.

[Experiment 030](experiments/030-spsc-rtt-repeatability.md) supports a conditional two-ring RTT tail
direction at its revision, while its small median gap remains unresolved. Neither saturated
throughput nor RTT measures one-way or specified-offered-rate latency.

Use the [effect-relative protocol](benchmark-methodology.md#repeatability-gate-for-small-comparisons):
match delivery, numerator, capacity, completion, and endpoint work, qualify the actual workload, then
compare independent interleaved processes and paired differences. Effects below observed dispersion
remain unresolved. An unexplained SPSC state is not a prerequisite for a separately qualified group.
Reopen diagnosis only for a discriminating hypothesis that would change a useful engineering decision.

A future `spmc-slot`/`spmc-serialized` comparison could ask whether overlapping consumer processing
retains a large complete-route advantage over whole-operation serialization under busy retries.
[Experiment 017](experiments/017-spmc-consumer-coordination.md) motivates that question historically,
but predates the publication-bound repair and does not predict its current magnitude. Both actual
routes and placement need fresh qualification. Ordered release needs a distinct question before
joining the comparison. This campaign leaves that measurement unrun.

## Open regions and selection rules

| Region | Useful question or limitation | Boundary |
| --- | --- | --- |
| Coordination topology | Can simultaneous producer/consumer competition expose a joint ownership or reuse invariant that existing MPSC/SPMC proofs cannot compose? | MPMC is deferred without that question. No runtime graph or generic participant framework. |
| Storage and lifetime | Does a new admission or reuse protocol change a demonstrated variable-byte contention or lifetime property? | Existing paired reservation answers the cooperative lifetime question. CAS admission or more participants alone is insufficient. |
| Lossy delivery | Is there a distinct freshness, delivery, or resynchronization invariant? | Independent readers and overwrite detection already exist; reader count alone is not a program. |
| Failure and recovery | What detector and policy make abandoned ownership recoverable without violating reuse? | Cooperative tests do not establish cancellation, leases, helping, or recovery. No crash-recovery infrastructure is planned. |
| Bounded verification | Can a tiny-state model distinguish a concrete untested history or C++ assumption? | Relate model assumptions to implementation tests; no broad formal-methods project. |
| Measurement | Does a stable effect support a discriminating workload, placement, PMU, or latency question? | Actual-route host qualification and claim-relative resolution are required. No open-ended SPSC diagnosis or parameter matrix. |

Waiting, fairness, counter lifetime, payload lifetime, and coherence cross these regions. Missing
coverage is not a TODO. A mechanism earns retention through a useful semantic, progress, or
structural distinction; speed on one host is neither required nor sufficient.

## Repository health

Mechanism-local additions should remain mostly local plus deliberate catalog/navigation updates.
Merge, join, and variable-record studies demonstrated that this is practical without benchmark
integration. Earlier benchmark-backed MPSC/SPMC additions crossed validation, dispatch, reporting,
and tests; commit `4b8aa01` repaired omitted producer-role reporting. That concrete drift supports
centralizing route identity, capabilities, role shape, capacity kind, and exploratory defaults in
benchmark-private descriptors. Timed loops and special semantic validation remain explicit.

Correctness-only builds can omit benchmark compilation, while default builds and CI verify the full
scaffold. Inventory tests should establish consistency rather than freeze asset totals. Build-source
identity and invocation checkout state are distinct: changing a checkout cannot relabel an old
executable. These are infrastructure responsibilities, not mechanism abstractions.

Ignored `results/` is disposable laboratory working material. Preserve a useful conclusion or
reusable method in its existing tracked home before discarding it; raw samples, temporary probes,
and host logs have no archival or compatibility rights. Experiment records remain understandable
without those local directories.

Reassess structure when recurring changes expose a correctness, navigation, or maintenance problem.
Do not add a registry, evidence archive, or shared mechanism implementation for hypothetical scale.
The next useful research question crosses this consolidation boundary; stop with healthy verification
and select a separately bounded question later.
