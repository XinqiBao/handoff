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

## Controlled performance comparisons: current boundary

The dedicated N150 can resolve some complete-workload directions under qualified placement and
fixed delivered frequency; it has no universal resolution or route ranking. In
[Experiment 022](experiments/022-fixed-frequency-mpsc-comparison.md), `mpsc-slot` beat
`mpsc-count` in all eight order-balanced, independent-process pairs (+23.40% to +46.24%). The
direction survived host controls; the exact magnitude and isolated publication cost remain
unknown. That result used yield retries and cannot be pooled with the current busy-retry workload.

[Experiment 023](experiments/023-spsc-measurement-stability.md) establishes the narrower
8 B / 64-slot scalar SPSC throughput limit: persistent, sometimes switching process rate states
remain despite passing relevant host checks. The
[supporting controls](experiments/README.md#supporting-measurement-investigations) did not establish
a clock, simple placement, initial-occupancy, or index-line stabilization explanation. They retain
useful negative evidence and instrumentation effects, but do not prove a microarchitectural cause.
Small scalar throughput gaps remain unresolved at this shape; stop open-ended diagnosis unless a
specific discriminating hypothesis would change a useful engineering decision. Busy contention
remains intended workload behavior.

[Experiment 030](experiments/030-spsc-rtt-repeatability.md) supports a conditional two-ring RTT
tail direction, while its small median gap remains unresolved. Exact observations and probe
chronology belong in the records. Neither RTT nor saturated throughput measures one-way or
specified-offered-rate latency; a new latency route needs its own contract and host gate.

Future comparisons follow the [effect-relative protocol](benchmark-methodology.md#repeatability-gate-for-small-comparisons):
first match semantics, numerator, capacity, completion, and endpoint work; then qualify the actual
workload and compare independent, interleaved processes with raw order and paired differences.
Small effects below observed dispersion stay unresolved. Do not make identification of the SPSC
state a prerequisite for a different qualified comparison group. Reopen that diagnosis only for a
specific test likely to change a useful conclusion.

[Experiment 031](experiments/031-busy-retry-mpsc-comparison.md) answers the current busy-retry
MPSC question: slot beat count in all eight balanced pairs after actual-route host qualification,
and the full route ranges did not overlap. The direction is resolved for this workload; its varying
magnitude does not support isolated publication-cost attribution or an enduring percentage. Stop
this comparison here, retaining all rows and the unresolved SPSC cause.

A future two-route `spmc-slot`/`spmc-serialized` comparison is justified only for the concrete
question of whether overlapping consumer ownership/processing retains a large complete-route
advantage over whole-operation serialization under busy retries. The shared work and final
producer-verified reuse boundary make that axis meaningful; mutex admission, overlap, release,
and discovery still differ. [Experiment 017](experiments/017-spmc-consumer-coordination.md)
suggests a potentially resolvable effect, with anomalous rows limiting its magnitude. Requalify both
actual routes and placement independently. This campaign reassessed that candidate without running
it; do not add ordered release to make a three-way ranking. No further MPSC or open-ended SPSC
measurement is currently justified without a distinct question.

Scalar SPSC (`basic`, `cache-line`, `cached-index`) is a separate lossless one-pair group, with
grouped SPSC comparing group operations only under matched group work. MPSC and SPMC each have
their own role topology and completion boundary. Reliable fan-out, pipeline, lossy observation,
and variable-size storage have different delivery or capacity contracts and no current peer for a
simple common-rate ranking.

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
| Coordination topology and progress composition | Completed bounded campaign: producer-owned merge and fixed two-branch write/join establish distinct ordering, progress, and reuse authorities. | Further MPMC or topology work needs a new joint invariant; see the conclusion below. No runtime graph or generic queue framework. |
| Storage and lifetime under concurrency | The bounded MPSC variable-record program established paired descriptor/byte admission and reuse across producer completion holes. | Further variable-storage work needs a distinct byte-lifetime or progress invariant; fixed-slot composition alone is insufficient. |
| Lossy multi-participant delivery | Independent lossy readers, overwrite detection, and range-based resynchronization are already implemented by the sequence payload ring. | A further program needs a distinct delivery or freshness invariant; another reader count alone is insufficient. |
| Failure and recovery semantics | Deferred region: abandoned producer, consumer, or stage ownership; cancellation, helping, identity, leases, or timeouts. | Requires a stated failure detector and recovery policy; cooperative progress tests do not answer it. |
| Bounded verification | Conditional method campaign: tiny-state exhaustive models or CBMC on a specific small invariant. | Add only when a model can distinguish real histories or expose an untested C++ assumption; relate model assumptions to implementation tests. |
| Controlled performance attribution | Conditional measurement campaign: PMU, coherence, workload, and placement hypotheses. | Needs a stable effect and a prepared host; the current N150 is suitable for semantics, stress, plumbing, and cautious complete-route observations. |
| Repository consolidation | The approved information and benchmark-discovery migration is complete; continue to watch integration drift. | The catalog covers all 24 assets. Benchmark-private route descriptors centralize names, workload support, role shape, and capacity presentation without changing mechanism code or research semantics. |

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

## Coordination topology campaign: framing

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

## Topology campaign: conclusion

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
gates pass, as does CI run `36509038687` at `285589efc0db07aa5e519d38fcb2e0ced7cfcb55`.
The mechanism uses the existing staged SPSC API, so it adds no benchmark dispatch or schema
changes. No local repository-health defect was exposed.

The next selected question was a fixed two-branch write/join. The
[two-branch join](mechanisms/two-branch-join.md) and [Experiment 020](experiments/020-two-branch-join.md)
establish one ordered owner per branch, disjoint branch result writes, two independent completion
acquires before join observation, and one final join release before physical reuse. The right
branch can complete later positions while the left branch holds an earlier one; the join remains at
that hole. Even after both finish, a held or cancelled join observation retains exact capacity.
Read-only fan-out already accounts for the slowest reader, but does not compose two writes. The
single-owner pipeline has one linear happens-before path. This fixed join establishes the dual
visibility and lifetime obligation without a dynamic graph. Debug, Release, ASan/UBSan, TSan,
tidy, and format gates passed locally. CI run `36511943487` passed at
`f79bf6b11c3c49b2112950a570c2cf6163d8a535`. There is no timed result: the semantic tests
answer the question, and the N150 has no spare core for four active roles plus a clean coordinator.

The selected MPMC per-slot state candidate was skipped. Shared MPSC slot availability already
defines unique producer claims, exact-generation publication, and consumer discovery; SPMC slot
completion already defines unique consumer claims, independent completion, and producer-discovered
reuse. The ordered worker-stage ring also demonstrates an intermediate completion frontier and
final release. Simply putting those transitions into one ring creates no new ordering authority
or joint lifecycle invariant supported by a sharper question. A future MPMC program needs a
specific simultaneous competition race, progress contract, or reuse obligation that these
existing proofs cannot compose. Adding it to fill a topology category would not improve this
campaign's answer.

The campaign question is answered for the selected cooperative, fixed-slot topologies. Shared
MPSC places global FIFO authority at a common claim cursor; producer-owned paths place merge
authority at the consumer and localize backpressure. The write/join places independent completion
authority at two branches and requires both publication paths before reading combined results;
only join release permits reuse. None of these contracts includes owner failure recovery, fair
thread scheduling, variable-size concurrent storage, or lossy observation. A stalled owner can
retain capacity indefinitely. Per-path and shared-ring capacities and ordering promises differ,
so the semantic study makes no general rate ranking. The N150 cannot support fine cache/coherence
attribution here. Those unresolved questions belong to the future regions above. The next useful
work would cross this campaign's boundary or require a distinct new invariant, so the campaign
stops here.

Both topology programs added mechanism-local headers, tests, and notes without touching benchmark
classification or output code. The join did not expose a local correctness or clarity defect in
existing mechanisms. The earlier benchmark integration observation remains a possible later
consolidation question, not a reason for infrastructure work in this campaign.

## Storage and lifetime program: selection

The next bounded question is whether two producers can pair a FIFO descriptor reservation with a
variable-size byte reservation before independently writing and completing, while the consumer
returns both credits only after its final read. The existing SPSC descriptor/payload ring already
handles aligned byte footprints and wrap gaps, but has no competing reservation or completion
hole. The fixed-slot MPSC slot-availability ring already handles out-of-order producer completion,
but every claim consumes one equal-sized physical slot. Neither establishes nonoverlapping byte
ranges or paired reclamation across a held variable-size claim.

The first program therefore uses a short mutex for joint descriptor/byte admission and permits
overlapping writes and independent completion outside that critical section. This isolates the
new ownership/lifetime invariant without adding a CAS protocol for two cursor dimensions. It has
one FIFO consumer, exact descriptor and byte capacities, direct ring-owned payload spans, and no
abandoned-owner recovery, multiple consumers, dynamic storage, or timed comparison. Deterministic
holes, exact byte credit, a wrapped gap, final-read gating, finite ordinal exhaustion, concurrent
integrity, memory-model review, and Debug/Release/sanitizer/quality gates decide completion.

Lossy multi-reader delivery was not selected: the sequence metadata/payload rings already accept
independent observers, detect overwrite, and expose a resynchronization range; two concurrent
observers are already tested. A reader-count timing run alone offers no new delivery invariant.
Failure recovery still lacks a failure detector and policy, and the architecture excludes crash
recovery infrastructure. Another MPMC frontier, topology variant, or fine N150 cost attribution
also lacks a sharper current question. These skips can be reconsidered only when new evidence
changes their information value.

## Storage and lifetime program: conclusion

The [MPSC variable-record ring](mechanisms/mpsc-variable-record.md) and
[Experiment 021](experiments/021-mpsc-variable-record.md) answer the selected cooperative question.
One reservation step pairs each FIFO descriptor ordinal with an aligned byte extent, including any
physical wrap gap. Producers write disjoint spans and publish independently; a later call can
return across an earlier hole, but the consumer stops at that hole. Its final release returns both
credits in descriptor order. Held observations retain both resources, and a released wrapped record
can fund an exact next claim while a newer record remains unfinished. The mechanism note gives the
C++ publication and reuse edges and the token lifetime contract.

Deterministic tests cover independent capacity limits, the wrapped hole, exact reuse, and finite
ordinal exhaustion; a two-producer mixed-length run checks 40,000 records. Local Debug, Release,
ASan/UBSan, TSan, format, and tidy gates passed. CI run `36577046502` passed at
`3a49ef9977a5f7b0220c58dc496e7535ee31c864`. These are semantic and integrity results, not
fairness, owner-failure recovery, or performance evidence. No timed comparison was added because
the mutex, direct payload spans, and two native capacities would change the workload contract.

One program answered the bounded question. A CAS-based paired reservation would vary admission
cost and progress properties without a current contention hypothesis; a variable-byte SPMC or MPMC
successor would presently combine this byte-credit result with established fixed-slot completion
frontiers without an identified new joint race. A small model lacks an untested history to
distinguish, and controlled fine cost attribution still lacks a suitable host. The other regions
remain as described above. There is no sufficiently justified next program now; the research loop
stops at this checkpoint without an owner-level decision.

## Repository health and information consolidation

The first post-topology reassessment found no case for a dedicated consolidation campaign.
Benchmark-backed MPSC and SPMC additions each crossed command validation, workload dispatch,
role reporting, CSV tests, and build bookkeeping; commit `4b8aa01` corrected omitted MPSC
producer-role reporting. Those changes show a real drift risk, but the later worker-stage,
merge, and join programs required only mechanism-local code, tests, and notes. Shared SPMC
invariants already use a small test helper, and the mechanism and experiment indexes still
cover the tracked records. No recurring defect or navigation failure justifies a registry,
shared mechanism implementation, or broad source reorganization. The architecture document's
outdated directory list was corrected locally. Revisit the narrow benchmark classification
surface when another benchmark route actually needs integration or exposes a mismatch.

The variable-record program again added only a mechanism-local header, tests, notes, and index
entries. It did not touch benchmark classification or output. This does not erase the earlier
reporting omission, but it adds no new recurring pressure for consolidation.

The later public-discovery review found a narrower information problem than source layout:
24 independently useful assets were listed without direct source/test/evidence routes, while
benchmark names and role/capacity classifications recurred in parsing and reporting. The
[mechanism catalog](mechanisms/README.md) now owns the complete asset map and conceptual reading
paths. The benchmark-private descriptor table owns executable route names, workload support, role
shape, and capacity presentation. The same CLI keeps historical workload-first commands and adds
mechanism-first exploratory runs. Exact contracts, memory-order arguments, experiment procedures,
negative results, and SHA evidence remain in their original tracked notes and records. No
mechanism source relocation, generic queue abstraction, extra teaching binary, or timed route for
semantic-only assets was justified. This migration makes discovery easier but does not answer a
new performance question or change the stopped research campaign.

At later checkpoints, preserve only recurring or consequential structural observations here with
the affected change history and practical impact. Fix correctness, stale facts, or a clear local
obstacle immediately; otherwise continue research. Revisit broader consolidation only if several
programs expose new recurring pressure or the present ownership model proves costly. Inspect Git
history and concrete changes to source layout, CLI/workload/output boundaries, tests/build, and
documentation before changing the structure. Local mechanism readability and explicit semantics
remain constraints; more abstraction is not an objective by itself.
