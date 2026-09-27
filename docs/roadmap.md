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

Consumer coordination and work sharing is the next distinct ownership
question: give each bounded publication exactly one consumer owner, then
define how consumer claim, completion, release, and reuse relate under a stalled
consumer. Begin with a fixed small topology and deterministic ownership tests;
compare against existing fan-out and MPSC results only where their delivery
contracts are explicitly aligned. Do not infer a universal rate ranking from
the producer-completion comparison.
