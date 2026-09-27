# Research Direction

This document owns the long-lived research landscape and the active research program. It is not a
completed-stage ledger or a fixed implementation queue. The [design space](design-space.md) owns
stable vocabulary, [mechanism notes](mechanisms/README.md) own implemented contracts, and
[experiment records](experiments/README.md) own measured evidence. Git preserves chronology.

## Established ground

The initial single-producer work covers bounded SPSC counter, layout, batch, bulk/burst, and staged
variants; sequence publication, reliable fan-out, and a fixed dependency pipeline; three record
storage layouts; and lossy sequence-addressed metadata and payload observation. Selected controlled
Linux comparisons establish conditional observations for one host and workload set, not a ranking.

The first multi-producer package compares whole-operation producer serialization with concurrent
claims and one ordered publication tail. The concurrent ring grants unique bounded slots but a
later `publish()` cannot return before earlier claimants publish. Its deterministic
publication-hole diagnostic establishes that later payload work can finish up to capacity while
visibility and publication-call return remain blocked. [Experiment 015](experiments/015-mpsc-ordered-publication.md)
observed higher complete-route throughput for the concurrent ring in every retained row on one
N150 placement, with substantial variation in the serialized control. It does not isolate the cost
of the frontier or predict another completion design. The [mechanism note](mechanisms/ordered-publication-mpsc.md)
owns the contract, C++ memory-order argument, and recovery limits.

## Research landscape

Status describes current knowledge and interest, not an execution phase or promise to build every
candidate. Waiting, fairness, counter lifetime, payload lifetime, and cache/coherence effects cut
across these areas.

| Area | Status | Research boundary |
| --- | --- | --- |
| Single-producer handoff and publication | Established baselines | Retain distinct ownership, batching, and sequence controls; revisit only for a new question. |
| Record storage and lossy observation | Established baselines; selective follow-up | Mixed lengths, independent limits, and lifetime may justify new work. |
| Producer coordination and completion | Active | Separate reservation, payload completion, publication-call return, ordered visibility, and reuse. |
| Consumer coordination and work sharing | Candidate | Give each publication one consumer owner; release and reuse differ from reliable fan-out. |
| Broadcast and dependencies | Fixed examples established; richer topology candidate | Explore small static fork/join or ordered stages when their invariants are isolatable. |
| Helping and contiguous stage progress | Exploratory | Relate worker completion to stage visibility without a runtime graph framework. |
| Selected MPMC composition | Exploratory, dependent on ownership work | Study only a bounded composition with a distinct semantic question. |
| Centralized versus partitioned coordination | Candidate architectural comparison | Producer-owned paths move ordering and polling costs to the consumer. |

This is a curated implementation catalog. A mechanism earns retention by exposing a meaningful
semantic, progress, or structural distinction, including an instructive intermediate result. Faster
throughput on one host is neither required nor sufficient. External designs are sources of ideas,
not the catalog's organizing names.

## Active program: producer completion and FIFO visibility

### Question and boundary

Can a producer finish its own publication operation without waiting for earlier claims while one
consumer still sees claim-order FIFO and storage is reused only after release? Where does completion
state live, and which participant discovers or advances the contiguous visible prefix? The ordered
tail and serialized routes above are established controls; do not reimplement them without a
specific reason.

Keep fixed inline slots, one consumer, lossless bounded delivery, claim-order FIFO for shared-ring
variants, and the existing scalar payload work initially. A claim charges capacity until consumer
release. A missing claimant is not cancelled or recovered: later producers may return from their
own publication calls, but the consumer must stop at the hole and capacity can eventually fill.
No candidate should be called lock-free or wait-free without an operation-specific proof.

This program excludes competing consumers, arbitrary dependency graphs, variable record storage,
production IPC or shared-memory lifecycle, abandoned-claim recovery, general wait-policy design,
and API/algorithm compatibility with external projects. Waiting remains explicit and comparable.

### Design points to investigate

1. **Ordered tail control:** the existing `mpsc-ordered` ring makes each finisher wait for its
   predecessor at one producer-maintained frontier. The serialized route is a broader architectural
   control, not an isolated measure of frontier overhead.
2. **Cooperative completion count:** study a small RTS-inspired design in which every finisher
   records completion and a finisher that closes a completed claim group advances visibility. A
   later call can return across a hole. DPDK RTS's head/tail counts publish a *whole completed
   claim group*; they do not independently discover every ready slot. Determine whether that
   group behavior is the useful design point, and make any adapted C++ counter and memory-order
   obligations explicit. Avoid importing DPDK's configuration and lifecycle machinery.
3. **Per-slot generation-tagged availability:** a producer independently marks its slot ready;
   the consumer discovers the contiguous available prefix from its next position. This moves
   completion representation to slots and contiguous-prefix discovery to the reader. Preserve a
   generation check across physical reuse. The Disruptor sequencer is inspiration, not a Java API
   or memory-model port.
4. **Producer-helped per-slot frontier, if informative:** combine independent slot completion
   marks with cooperative producer advancement only if it cleanly isolates *who* discovers the
   prefix from *where* readiness lives. Skip it if it merely duplicates another mechanism's
   answer or obscures the completion protocol.
5. **Partitioned producer-owned SPSC paths, conditional control:** use one path per producer and
   consumer polling/merge only when a clear merge policy makes it a meaningful comparison with
   shared reservation. Per-producer FIFO is natural; global FIFO requires an explicit ordering
   authority and may erase the intended benefit. State that semantic difference rather than
   reporting its rate as an equivalent FIFO queue.

The first two new shared-ring points form the core program. The helped variant and partitioned
control are evidence-driven additions, not a quota of mechanisms. Preserve an intermediate
implementation only when its distinction remains useful after comparison.

### Conceptual contrasts before implementation

| Point | Completion, call return, and visibility | Reuse, progress, and proof focus | Distinguishing observation |
| --- | --- | --- | --- |
| Ordered tail | No separate completion mark; each owner advances the shared tail in claim order and returns then. | Consumer release gates reuse. A stalled owner blocks later calls and visible progress; the existing acquire/release chain proves payload visibility. | Later payload work can finish but its `publish()` cannot return. |
| RTS-like count | A shared count records each finisher; a catching finisher advances the group tail. Later calls may return across a hole, but newer unfinished claims may delay visibility even after that hole closes. | Consumer release still gates physical reuse. C++ atomic read-modify-write ordering must carry every completed payload to the group-advancing store; finite count/position exhaustion and stalled group members need explicit policy. | Pause an early and then a newer claimant to expose group-tail lag separately from call return. |
| Slot availability | Each finisher release-marks its position and returns; the consumer acquire-checks generation tags from its next required position and stops at the first hole. | Release gates reuse; generation disambiguates old readiness. Prove payload visibility, metadata reset/reuse, finite sequence handling, and no concurrent slot access. A stalled owner blocks FIFO observation but not later calls. | Closing the earliest hole makes the already-ready contiguous prefix discoverable without another producer. |
| Producer-helped slots, conditional | Slot marks record completion; producers also attempt contiguous frontier advancement. | Reuse remains consumer-gated. Helping races, acquire/release chaining, and hot-frontier traffic add proof and coherence cost. | Compare consumer discovery with producer discovery of the same ready prefix. |
| Producer-owned paths, conditional | Each producer publishes independently; the consumer polls paths and chooses a merge policy. | Each SPSC release gates its own reuse; a stalled path need not stop others under per-producer FIFO. Global FIFO needs an additional ordering rule. Poll fairness and idle-path scanning matter. | Separate producer-claim contention from consumer polling and ordering costs, without claiming equivalent semantics if global order changes. |

The RTS-like and slot-availability designs can share fixed payload work and claim-order FIFO, but
their progress guarantees differ even when both let a later publication call return. The
partitioned control is an architectural comparison only with a stated merge contract.

### Comparison and evidence

For each candidate, document producer ownership and reservation; payload completion;
publication-call return; the visible contiguous frontier; consumer release and bounded reuse;
ordering; metadata placement; shared and slot-local contention; stalled-owner behavior; progress
guarantees and limits; finite counters and generation/wrap policy; and C++ happens-before edges.
Distinguish semantic properties, implementation properties, host-specific observations, and broader
hypotheses in every conclusion.

Before rates, use controlled barriers/latches to pause the earliest owner, finish later payloads,
observe which later publication calls return, verify that no consumer crosses the hole, fill the
bounded ring, then close the hole and verify release/reuse through wrap. Include unique claims,
sustained concurrent integrity, full/empty boundaries, finite-counter behavior, and declared
unfinished-claim obligations. Reason about slot lifetime, release/acquire paths, and cache-line
sharing; sanitizers support exercised paths but cannot replace the argument. Avoid scheduler-sleep
tests and blanket `seq_cst` ordering.

After correctness gates pass, run one equivalent complete-handoff throughput workload against the
established controls. A separate untimed hole diagnostic should report reservation, completed
payload, returned publication calls, visible prefix, consumer completion, and backpressure at a
controlled snapshot. Add higher contention, capacity, or payload sensitivity only to distinguish
a concrete hypothesis or confound. Preserve raw trials and provenance; one coherent question may
own several commands in one experiment record. Smoke timings are plumbing checks, not evidence.
The previous N150 result is a baseline observation, not a target to beat.

### Checkpoints and completion

Treat each coherent design point or evidence question as a package: implement, test, diagnose,
measure when justified, interpret, review, document, commit, push, and check CI. At each checkpoint,
reassess whether the next candidate still isolates a distinct question; revise, reorder, skip, or
add a small intermediate point when evidence warrants it. Routine implementation and workload
adjustments are within the program. Stop for a material change to the project's research identity,
portability or dependency boundary, a major harness redesign, a premise-invalidating semantic
choice, unavailable required hardware, or CI that remains red after reasonable repair.

The program ends after a synthesis, not after the last benchmark. Compare what each design teaches
about completion, visibility, reuse, progress, and measured contention; identify robust observations
and host-specific limits; retain only distinct useful mechanisms; record skipped candidates and
unresolved questions. Move stable conclusions into the design space, exact contracts into mechanism
notes, and evidence into experiment records. Condense this active section to a durable conclusion
and identify the next frontier. No planned experiment record is needed before a defensible protocol
and actual execution.
