# Research Direction

This document owns the long-lived research landscape and current direction. It is not a
completed-stage ledger or fixed implementation queue. The [design space](design-space.md) owns
stable vocabulary, [mechanism notes](mechanisms/README.md) own implemented contracts, and
[experiment records](experiments/README.md) own measured evidence. Git preserves chronology.

## Established ground

The initial single-producer work covers bounded SPSC counter, layout, batch, bulk/burst, and staged
variants; sequence publication, reliable fan-out, and a fixed dependency pipeline; three record
storage layouts; and lossy sequence-addressed metadata and payload observation. Selected controlled
Linux comparisons establish conditional observations for one host and workload set, not a ranking.

The first multi-producer package compared whole-operation producer serialization with concurrent
claims and one ordered publication tail. Its [Experiment 015](experiments/015-mpsc-ordered-publication.md)
established publication-call blocking across a hole and recorded one controlled N150 comparison.
The [producer-completion program](experiments/016-mpsc-producer-completion.md) then added a
cooperative completion count and per-slot generation-tagged availability to separate call return
from FIFO visibility. Exact contracts and memory-order arguments live in their mechanism notes.

## Research landscape

Status describes current knowledge and interest, not an execution phase or promise to build every
candidate. Waiting, fairness, counter lifetime, payload lifetime, and cache/coherence effects cut
across these areas.

| Area | Status | Research boundary |
| --- | --- | --- |
| Single-producer handoff and publication | Established baselines | Retain distinct ownership, batching, and sequence controls; revisit only for a new question. |
| Record storage and lossy observation | Established baselines; selective follow-up | Mixed lengths, independent limits, and lifetime may justify new work. |
| Producer coordination and completion | Established shared-ring comparison | Separate reservation, payload completion, publication-call return, ordered visibility, and reuse. |
| Consumer coordination and work sharing | Next frontier | Give each publication one consumer owner; release and reuse differ from reliable fan-out. |
| Broadcast and dependencies | Fixed examples established; richer topology candidate | Explore small static fork/join or ordered stages when their invariants are isolatable. |
| Helping and contiguous stage progress | Exploratory | Relate worker completion to stage visibility without a runtime graph framework. |
| Selected MPMC composition | Exploratory, dependent on ownership work | Study only a bounded composition with a distinct semantic question. |
| Centralized versus partitioned coordination | Candidate architectural comparison | Producer-owned paths move ordering and polling costs to the consumer. |

This is a curated implementation catalog. A mechanism earns retention by exposing a meaningful
semantic, progress, or structural distinction, including an instructive intermediate result. Faster
throughput on one host is neither required nor sufficient. External designs are sources of ideas,
not the catalog's organizing names.

## Producer-completion program: conclusion

The question is answered for bounded fixed-slot shared MPSC rings with two producers,
one FIFO consumer, and no abandoned-claim recovery. The ordered tail allows
concurrent payload work but a later publication call waits for its predecessor.
The [completion-count ring](mechanisms/completion-count-mpsc.md) lets later calls
return across a hole, yet publishes only a whole completed claim group; a newer
unfinished claim can keep an earlier ready prefix invisible. The
[slot-availability ring](mechanisms/slot-availability-mpsc.md) also lets later
calls return and lets the consumer discover that prefix immediately after its
earliest hole closes. All shared-ring routes charge capacity until consumer
release, stop at finite position exhaustion, and leave an abandoned claim
unrecovered. None earns a general lock-free or wait-free claim.

[Experiment 016](experiments/016-mpsc-producer-completion.md) contains the
deterministic progress diagnostic, correctness gates, and the one controlled
N150 complete-handoff comparison. On that host and workload, every retained
block ordered slot availability above completion count above ordered tail above
producer serialization. This is a conditional complete-route observation, not
an isolated coordination cost or cross-host ranking. The serialized control
varied most; host endpoint checks cannot prove each trial's frequency or
absence of interference. Experiment 015 remains the historical two-route
comparison at its own exact SHA.

Both new rings are retained for their distinct visibility contracts. A
producer-helped per-slot frontier was skipped: the consumer already discovers
the ready prefix without help, and an added shared frontier would need a
separate bottleneck hypothesis to justify its helping races and coherence
traffic. Producer-owned SPSC paths were skipped as an equivalent control:
per-producer FIFO needs a consumer merge rule, whereas global claim-order FIFO
requires an ordering authority that changes the comparison. Neither is a
quota item for this program. A future targeted experiment may revisit either
after stating its own semantic and measurement question.

Unresolved questions include which cache/coherence costs caused the N150 rate
gaps, whether the direction persists with more producers or different payloads
and capacities, and how a separate recovery protocol could handle an abandoned
claim without violating bounded reuse. Those require their own protocols and
evidence; the current program does not answer them.

## Next frontier

### Question and boundary

For one producer and competing consumers of a bounded fixed-slot ring, how do
unique acquisition, processing completion, return from `release()`, and safe
producer reuse relate when consumers finish out of acquisition order? This is
work sharing: each published position has one consumer owner. It is distinct
from reliable fan-out, where every reader must receive every publication.
Fan-out is a semantic contrast, not an equivalent throughput control.

Keep the producer single and its publication ordered so consumer coordination
is the variable. Begin with a fixed small consumer count, one outstanding
position per consumer, exact slot capacity, and finite non-wrapping logical
positions. The mechanism guarantees at most one owner per acquired position,
producer-to-owner visibility, and no overwrite while an owner may still access
its slot. Under cooperating participants, every published position is eventually
acquired and its owner calls `release()` after its final access. This is not an
exactly-once successful processing guarantee: user work can fail, and the ring
cannot verify its effects. An abandoned acquired position retains capacity;
recovery, cancellation after acquisition, owner death, leases, and timeouts are
separate questions. Producer claim cancellation before publication can follow
the existing single-producer token contract.

The conceptual lifecycle is `free -> producer-owned -> published ->
consumer-owned -> consumer-complete -> reusable`. Acquisition assigns a unique
FIFO position; it does not mean processing has finished. Consumer completion is
the owner's final slot access followed by a release operation. A release call
may either wait for its turn or record completion and return; the latter does
not by itself make the slot reusable. The producer may overwrite a physical
slot only after it has acquired proof that the previous owner finished. In the
initial cyclic mapping, the producer publishes positions in order, so an early
unfinished slot eventually blocks wrap even if later consumers finish. FIFO
describes assignment of positions, not wall-clock processing completion or a
fair distribution of work across consumers.

### Representative design points

1. **Serialized consumer control.** Use a small fixed ring whose competing
   consumer operations hold one mutex across acquisition, payload access, and
   release; keep the producer independent. This establishes the same complete
   handoff contract but admits only one consumer operation at a time. It is a
   useful control for the cost of allowing overlapping work, not an isolated
   atomic-cost baseline. Keep its implementation and contract small and explicit;
   do not pass the existing SPSC ring's single-thread consumer role among workers.
   A stalled owner prevents other consumers from acquiring; the producer can
   fill remaining capacity.
2. **Shared claim, ordered release.** A CAS grants each consumer a distinct
   published position. A consumer finishing a later position waits for the
   preceding release cursor before returning from `release()`, then advances
   that cursor. The producer gates reuse on the contiguous cursor. This asks
   whether concurrent ownership and processing are useful even when completion
   calls serialize at the release frontier. A stalled early owner lets later
   consumers acquire and do work, but their release calls cannot return; an
   abandoned owner permanently blocks the frontier. A nonblocking release
   attempt may make this progress property testable without scheduler timing.
3. **Independent per-slot completion, producer-discovered reuse.** The unique
   claim cursor remains, but each owner release-marks its own generation-tagged
   slot and returns independently. The sole producer checks completed tags from
   its oldest unreusable position and advances a contiguous reusable prefix
   before admitting a new publication. This asks whether later workers can
   finish and return across a hole without making unsafe reuse possible. A
   stalled owner eventually stops the producer at capacity; when it finishes,
   the producer alone can discover an already-completed prefix, even if a newer
   owner is unfinished or no consumer acts again. Tags must identify the exact
   logical generation. This is a distinct return/progress contract from ordered
   release, not merely another implementation of its waiting call.

These are two concurrent mechanisms plus one serialized control, not a quota.
An RTS-like shared completion count is not an initial candidate: with a newer outstanding
claim it can withhold an already-completed release prefix, repeating a known
producer-side distinction without a new consumer question. Consumer-helped
frontier advancement is also conditional; producer-only discovery already
answers the basic hole question, while helping adds races and shared traffic.
Per-slot local reuse without a global released cursor, as in
[Rigtorp's bounded turn-based queue at a pinned revision](https://github.com/rigtorp/MPMCQueue/blob/b9808ede08f26fa9df4df4e081d19cace8f6c6ea/include/rigtorp/MPMCQueue.h)
is a possible focused follow-up if the prefix representation itself
becomes a question. With one ordered producer and cyclic positions, it cannot
skip the first still-owned physical slot at wrap. That queue uses shared
producer and consumer position claims plus per-slot acquire/release turn tags;
its `pop` moves and destroys an object before releasing the slot, unlike the
planned direct-slot processing lifetime. Its broader MPMC and dynamic-storage
concerns are outside this program. An MPMC composition changes both ownership
sides and is outside this program unless the consumer contract
proves unintelligible in SPMC; the current model gives no such need.
No route should claim operation-wide lock-free or wait-free progress merely
because its ownership cursor uses CAS: a stalled owner can block reuse and a
claimant can starve under contention.

### Diagnostics and evidence

Build deterministic ownership and progress tests before any timed comparison.
Publish at least three positions; have C0 acquire N and wait on a latch, C1
acquire N+1 and finish, and C2 acquire N+2 and finish. Verify unique ownership,
which calls return, the reusable prefix, full-capacity rejection, and the
absence of overwrite. Then close C0's hole and verify who advances reuse and
whether N+1 becomes reusable while a still newer acquired position remains
unfinished. Repeat across physical wrap and near the finite counter limit.
Use latches/barriers and explicit state checks, not sleeps or scheduler luck.
Keep this diagnostic untimed and separate from benchmark rates.

Each mechanism note and test suite must justify the C++ synchronization chain:
producer publication to consumer read; unique consumer claim; last consumer
slot access to completion state; and completion state to the producer's next
write of that physical slot. Ordered release must carry earlier owners' reads
through the advancing tail; per-slot completion needs acquire observation of
the matching generation before reuse. Cover empty/full, failed operations,
adversarial holes, repeated reuse, payload integrity, finite exhaustion,
abandoned-owner consequence, and concurrent distribution without claiming
fairness from a passing integrity run. ASan/UBSan and TSan support exercised
paths but do not prove the happens-before argument. Avoid blanket `seq_cst`.

After correctness gates, use one equivalent complete-handoff workload: one
producer, a fixed small set of consumers, the same fixed payload generation and
validation, one final release per publication, and rate measured as fully
processed and producer-verified reusable publications per second. The timed
phase ends only after all worker releases and producer discovery of the final
reusable prefix; add equivalent final-state checks for every route. Record
per-consumer counts separately from correctness and throughput. Keep warmup,
retries, placement, timed boundaries, and payload work equivalent. Verify full role
placement on the controlled Linux host and use the exact-SHA, CI-green workflow
in [Reproducibility](reproducibility.md). The historical N150 has four physical
cores, so one producer plus three consumers leaves no spare core for a pinned
coordinator; choose a defensible two-consumer canonical placement there, while
the three-owner hole remains an untimed diagnostic. Add targeted sensitivity
only for a concrete hypothesis or confound. Compare complete routes rather
than calling a rate gap the cost of one atomic or inferring fairness from rate.

Implement and validate one route at a time, interpret its progress diagnostic
and canonical measurement, then decide whether the next point still isolates a
useful distinction. The implementation program may reorder closely related
packages, skip a redundant route, or add one small intermediate variant with a
stated question. It should close with a synthesis of ownership, completion,
release, reuse, retained mechanisms, skipped candidates, deterministic facts,
host-specific observations, unresolved causes, and the next frontier. Move
stable terms into [Design Space](design-space.md), exact contracts into mechanism
notes, evidence into experiment records, and condense this active detail when
the program is complete. Do not automatically designate MPMC as next.
