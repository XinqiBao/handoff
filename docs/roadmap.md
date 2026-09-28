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
| Consumer coordination and work sharing | Established three-route comparison | Unique acquisition, release-call return, and producer reuse have distinct progress contracts. |
| Broadcast and dependencies | Fixed examples established; richer topology candidate | Explore small static fork/join or ordered stages when their invariants are isolatable. |
| Ordered worker-stage progress | Established fixed mechanism | Downstream discovers a contiguous completed prefix; only downstream release gates reuse. |
| Selected MPMC composition | Exploratory, dependent on ownership work | Study only a bounded composition with a distinct semantic question. |
| Centralized versus partitioned coordination | Candidate architectural comparison | Producer-owned paths move ordering and polling costs to the consumer. |
| Multi-participant storage and lossy delivery | Conditional candidates | Range ownership, payload lifetime, and detectable gaps need a specific question before combining dimensions. |
| Abandoned ownership and recovery | Deferred protocol frontier | Failure detection, helping, cancellation, or time semantics exceed the current cooperative contracts. |
| Performance attribution | Deferred measurement frontier | Source-level or counter evidence needs a stable effect and a suitable measurement environment. |

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

## Consumer-coordination program: conclusion

The bounded fixed-slot SPMC question is answered for one ordered producer, two
competing consumers in the canonical workload, and no abandoned-owner recovery.
The [serialized control](mechanisms/serialized-consumer-spmc.md) holds a mutex
from acquisition through release, so an early stalled owner prevents later
acquisition. The [ordered-release ring](mechanisms/ordered-release-spmc.md)
grants unique positions by CAS and permits overlapping processing, but later
release calls wait for the shared predecessor cursor. The
[slot-completion ring](mechanisms/slot-completion-spmc.md) also grants unique
positions, lets later owners release and return across a hole, and makes the
sole producer acquire-discover the contiguous safe reuse prefix. A returned
release is not by itself physical reuse. The earliest unfinished physical slot
eventually blocks cyclic publication in every route.

All three keep direct slot access valid through the owner's final read, charge
exact capacity, and stop at finite logical position exhaustion. The tests
establish unique acquisition and payload integrity under cooperation, not
exactly-once successful user effects or fairness. An abandoned owner retains
capacity; no route provides recovery or operation-wide lock-free/wait-free
progress. Exact C++ ordering arguments and ownership obligations live in
the mechanism notes.

[Experiment 017](experiments/017-spmc-consumer-coordination.md) contains
three-owner untimed hole facts, correctness gates, and a controlled two-worker
N150 comparison at exact CI-green SHA. Every canonical block measured slot
completion above ordered release above serialization as complete routes.
One ordered row dipped, and a targeted repeatability bracket found a high
slot row; the cause and stable gap sizes remain unresolved. Host endpoint
checks did not show thermal throttling but cannot exclude transient frequency
or interference. Fan-out rates are not equivalent delivery.

The three routes are retained because they separate acquisition overlap,
release-call return, and producer discovery. A shared consumer completion
count was skipped: it can delay an already-completed prefix behind a newer
unfinished claim without a new consumer-side question. Consumer-helped
reclamation and local turn-based reuse were skipped: producer-only discovery
already answers the basic hole question, and one ordered cyclic producer
cannot skip the earliest still-owned physical slot. Producer-owned SPSC
partitioning changes FIFO merge authority. MPMC composition, owner failure,
leases, timeouts, and cancellation after acquisition remain separate scope.

## Ordered worker-stage program: conclusion

The fixed [ordered worker-stage ring](mechanisms/ordered-worker-stage.md) answers the selected
question for one ordered producer, two competing stage workers, one ordered downstream consumer,
and cooperative completion. Workers CAS-claim unique published positions and may mutate their
slots. Each independently tags completion and returns; the downstream consumer alone discovers
the contiguous completed prefix. A later returned worker completion cannot cross an earlier hole.
When that hole closes, already-completed successors become visible without more worker action,
even while a newer worker remains unfinished. The downstream consumer reads transformed payloads
in order and its final release alone authorizes producer reuse of each physical slot.

The complete producer-to-worker-to-downstream-to-producer C++ ordering and lifetime argument lives
in the mechanism note. [Experiment 018](experiments/018-ordered-worker-stage.md) records
latch-controlled holes, exact-capacity release gating, wrap and finite-position behavior, a
30,000-position concurrent integrity run, and local Debug, Release, sanitizer, and static-analysis
gates. This is a distinct stage-progress boundary from the SPMC producer-discovered reuse prefix
and the single-owner dependency pipeline. An abandoned stage owner remains unrecovered and can
eventually block bounded publication. Unique claims do not imply exactly-once external effects.

One mechanism answered the semantic question, so no helped frontier, count representation, second
stage, or benchmark was added. Deterministic tests already show the progress distinction, while
the four-core N150 would have no spare core for a clean fifth-role timing arrangement. No
performance claim follows. DPDK SORING and the Disruptor dependency material remain inspirations,
not compatibility targets.

No next program is selected by this result. A fixed fork/join would need a specific branch/join
ownership question; producer-owned SPSC partitioning would need an explicit global FIFO merge
contract. Owner recovery and fine cache/coherence attribution still require separate protocols or
measurement environments. Those are durable frontiers, not implicit follow-on work.

Selected MPMC could combine existing producer and consumer protocols, but
topology completion alone does not justify another ring; require a new joint
state or progress invariant. Multi-participant variable storage, lossy
observation, and abandoned-owner recovery each change lifetime or delivery
semantics enough to deserve their own bounded questions later. The N150
complete-route rates are factual observations at recorded revisions, not a
stable atomic/cache/coherence cost attribution. Its four cores, ordinary OS
activity, observed variability, and restricted perf access make fine cost
explanation a weaker immediate investment than the stage question. See
[reproducibility](reproducibility.md) and
[benchmark methodology](benchmark-methodology.md) for this evidence boundary.
