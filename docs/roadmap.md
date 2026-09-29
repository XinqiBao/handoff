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

The [consumer-coordination program](experiments/017-spmc-consumer-coordination.md) separated unique
consumer acquisition, release-call return, producer-discovered reclamation, and physical reuse. The
[ordered worker-stage program](experiments/018-ordered-worker-stage.md) separated worker completion,
downstream discovery of a contiguous stage prefix, and final downstream release. These are
cooperative fixed-slot contracts. An abandoned owner remains a hole; neither tests nor N150 rates
establish failure recovery, fairness, or a general performance ranking. Producer, consumer, and stage
programs have now substantially covered the pattern of out-of-order completion followed by a
contiguous safe progress frontier. Another frontier representation needs a new invariant.

## Research landscape

Status describes current knowledge and interest, not an execution phase or promise to build every
candidate. Waiting, fairness, counter lifetime, payload lifetime, and cache/coherence effects cut
across these areas.

| Region | State and future question | Boundary |
| --- | --- | --- |
| Established controls | SPSC ownership, batching, sequence publication, reliable fan-out, fixed dependency, storage layouts, lossy observation, and shared-ring MPSC/SPMC/stage progress have distinct recorded contracts. | Revisit only for a new invariant or controlled question; do not add another frontier variant by default. |
| Coordination topology and progress composition | Active campaign: compare where ordering authority, progress discovery, backpressure, and reuse responsibility live. | Fixed, cooperative, bounded topologies; partitioned producer paths, a justified branch/join, or selected joint MPMC state. No runtime graph or generic queue framework. |
| Storage and lifetime under concurrency | Future region: variable-size reservations, byte-range ownership, wrap padding, descriptor/payload coupling, holes, and reclamation with multiple participants. | Preserve exact byte and object lifetimes; avoid treating fixed-slot coordination as proof for variable records. |
| Lossy multi-participant delivery | Future region: independent reader progress, overwrite, generation gaps, and freshness versus completeness. | Keep detectable loss separate from reliable fan-out and backpressure. |
| Failure and recovery semantics | Deferred region: abandoned producer, consumer, or stage ownership; cancellation, helping, identity, leases, or timeouts. | Requires a stated failure detector and recovery policy; cooperative progress tests do not answer it. |
| Bounded verification | Conditional method campaign: tiny-state exhaustive models or CBMC on a specific small invariant. | Add only when a model can distinguish real histories or expose an untested C++ assumption; relate model assumptions to implementation tests. |
| Controlled performance attribution | Conditional measurement campaign: PMU, coherence, workload, and placement hypotheses. | Needs a stable effect and a prepared host; the current N150 is suitable for semantics, stress, plumbing, and cautious complete-route observations. |
| Repository consolidation | Conditional future engineering campaign after accumulated maintenance evidence. | Review history, structure, CLI/output schemas, tests/build, and documentation ownership without hiding mechanism semantics in generic abstractions. |

These regions interact but are not a matrix to implement. Fixed-slot topology work may reveal a
later storage or recovery question; crossing that boundary requires a new campaign decision. The
verification and performance regions provide methods only when they answer a concrete question.

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

Experiment 018 selected no next program. The campaign below is a subsequent planning decision, not
an inference that the worker-stage result requires another mechanism. Owner recovery and fine
cache/coherence attribution still require separate protocols or measurement environments.

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

## Active campaign: coordination topology and progress composition

**Umbrella question:** How does the location of coordination change ordering authority, progress
composition, backpressure, ownership, and storage lifetime in small bounded cooperative topologies?
The shared vocabulary is claim ownership, completion, call return, visibility, release,
reclamation, and actual reuse. The purpose is to compare where each obligation lives, not to fill
every SPSC/MPSC/SPMC/MPMC cell or repeat the contiguous-frontier pattern.
Concurrent variable-size storage, lossy delivery, failure recovery, dynamic dependency graphs,
production IPC, and general performance tuning belong outside this campaign. Bounded verification
or timing may support an in-scope question, but is not a program quota.

The initial hypothesis is **two producer-owned SPSC paths feeding one merge consumer**. Recent
shared MPSC routes establish claim-order global FIFO and its publication holes; partitioning gives
each producer its own FIFO and moves selection, polling, fairness policy, and backpressure effects to
the merge. Define per-producer FIFO and a deterministic merge rule before implementation. Do not
claim global FIFO without an explicit ordering authority. Compare semantics with the shared routes;
any rate comparison must account for different capacity and ordering contracts. A sharper variant
may use an explicit downstream handoff if it exposes a distinct merge authority.

Two further candidates are conditional, not queued work:

- A **fixed two-branch join** is useful only if independent branch ownership and completion create a
  visibility and lifetime obligation beyond the existing read-only fan-out minimum and single-owner
  pipeline. Define who may write each part of a slot, when join may acquire both results, how each
  branch's last access precedes join visibility, and why final release alone permits producer reuse.
  If the protocol reduces to `min(branch A, branch B)` over existing ordered readers, skip it.
- A **selected MPMC per-slot state protocol** is useful only if simultaneous producer and consumer
  competition creates a joint generation lifecycle that cannot be explained by mechanically
  composing the existing MPSC and SPMC rings. Require an explicit transition argument from reusable
  through producer ownership, publication, consumer ownership, completion, and next-generation
  reuse. Skip it if no new invariant appears.

At each checkpoint, ask what the completed program established, what remains unknown in this region,
which candidate offers a new ownership or progress rule, whether existing mechanisms already answer
it, and whether deterministic tests can resolve it. A new in-scope candidate may replace, reorder,
or displace the listed candidates. Record attempted and skipped programs and the reason for each
decision here; link exact mechanism notes and experiments rather than copying their evidence. A
program may finish with semantic tests, concurrent integrity, memory-model reasoning, and relevant
quality gates alone. Timed data is optional and must serve a precise supported claim.

Each program has a question, non-goals, explicit contract, adversarial deterministic interleavings,
concurrent integrity and finite/wrap coverage as applicable, and a reviewed C++ happens-before and
lifetime argument. After appropriate local gates, review the complete diff, update the mechanism
note, experiment record where useful, and this campaign state; make coherent commits, push, and wait
for required CI. Re-read these records before choosing another program. Normal checkpoints,
candidate skips, small local repairs, and ordinary CI repair do not require owner review. Use the
owner only for a consequential change to scope, dependencies, portability, measurement environment,
or strategy that repository evidence cannot resolve.

Stop when the umbrella question is sufficiently answered, remaining candidates add no distinct
knowledge, the next useful question belongs to another region, evidence needs unavailable hardware,
complexity outweighs information gain, repository health requires a project-level decision, or the
campaign premise fails. At completion, leave a synthesis here identifying attempted and skipped
programs, changed selection, established facts, limits, structural observations, open questions,
and the reason for stopping. One program is not the default conversation boundary.

## Topology campaign checkpoint: producer-owned merge

The [two-path merge](mechanisms/two-path-merge.md) and [Experiment 019](experiments/019-two-path-merge.md)
answer the initial program. A consumer-owned rotating poll chooses between two independent staged
SPSC heads. Each path is FIFO and has exact capacity `C`; the merged order follows acquisition,
not global producer claim or publication order. An unpublished or held first path cannot block the
second path, but its idle capacity cannot be borrowed either. Final consumer release is local to
the selected path; a held first-path slot can coexist with repeated second-path wrap. The
underlying SPSC counters permit bounded unsigned wrap, unlike finite shared MPSC position tags.

The deterministic and concurrent tests establish these cooperative semantics. No rate result is
claimed: shared MPSC has one `C`-slot capacity and claim-order FIFO, while this topology has two
`C`-slot capacities and no cross-producer FIFO. A fair timing comparison would need an explicitly
different question and workload contract. Local Debug, Release, ASan/UBSan, TSan, tidy, and format
gates pass. The mechanism uses the existing staged SPSC API, so it adds no benchmark dispatch or
schema changes. No local repository-health defect was exposed.

The next selected question is a fixed two-branch write/join. It has distinct value only with
independent writes to disjoint fields, two acquire paths into one join observation, and final join
release gating physical reuse. Read-only fan-out already answers a minimum-of-reader-progress
question; the single-owner pipeline has one linear handoff. A fixed branch/join can test the
new visibility obligation without a generic graph or dynamic policy. The selected design will
keep one ordered owner per branch and one ordered join consumer; a branch's held position will
create a join hole while the other branch may finish later positions. This program remains
untimed unless its semantic tests expose a concrete rate question.

## Repository health and later consolidation

Recent history shows real benchmark integration cost: the SPMC package touched command validation,
implementation metadata, workload dispatch, output, CLI/CSV tests, and CMake in addition to the
mechanisms; the MPSC package needed a later reporting fix for producer roles. The current
`command.cpp` and `output.cpp` still repeat implementation classifications. This is a bounded
observation about change surface and drift risk, not evidence that a registry or broad rewrite would
improve the code. Experiment 018 needed no benchmark integration, so it did not incur that cost.

At later checkpoints, preserve only recurring or consequential structural observations here with
the affected change history and practical impact. Fix correctness, stale facts, or a clear local
obstacle immediately; otherwise continue research. A dedicated consolidation campaign becomes
eligible when several programs show the same maintenance pressure, add/remove cost is dominated by
unrelated edits, or navigation and documentation ownership materially degrade. It should inspect
these observations and Git history, then review source layout, mechanism catalog, CLI/workload/output
boundaries, test and CMake structure, documentation, and CI. Any changes must preserve local
mechanism readability and explicit semantics; more abstraction is not an objective by itself.
