# Design Space

This document is the stable vocabulary for classifying mechanisms and experiments. It is not an
implementation schedule and does not imply that every combination should be built.

## Experiment classes

A **mechanism-isolation experiment** changes one important design property while holding relevant
workload and harness behavior as constant as practical. An **implementation comparison** compares
representative implementations whose differences may not be reducible to one property. Results
must identify which class applies.

The goal is conditional understanding: which mechanism behaves well under which conditions, why,
and with which semantic or structural trade-offs. The project does not seek a universal ranking.

## Producer and consumer topology

- **SPSC**: one producer and one consumer.
- **MPSC**: multiple producers and one consumer.
- **SPMC**: one producer and multiple consumers.
- **MPMC**: multiple producers and multiple consumers.

Topology alone does not define delivery. Multiple consumers may share work or independently observe
the same publications.

## Delivery model

- **Work sharing**: one consumer acquires exclusive ownership of each published
  position. Unique acquisition does not guarantee successful execution of user
  work; that requires participant cooperation and, for external effects, a
  separate protocol.
- **Broadcast (fan-out)**: independent consumers observe each required message.

For work sharing, distinguish FIFO **acquisition** of positions from processing
completion order, return from a consumer's release call, and producer-visible
reclamation. A consumer can finish later work before an earlier owner, while
cyclic slot reuse may still wait for the earliest unfinished physical slot.
State whether release waits for a contiguous predecessor or records independent
completion, and name who discovers the reusable prefix. A worker's successful
release is a handoff fact; fairness is a separate observed distribution, not a
consequence of FIFO acquisition or aggregate throughput.

The lifecycle is `free -> producer-owned -> published -> consumer-owned ->
consumer-complete -> reusable`. **Consumer completion** follows the owner's
last slot access and is represented by a release cursor or a matching slot
tag. **Release-call return** says the owner finished its protocol; it may
precede producer discovery. **Producer-visible reclamation** is the prefix
that the producer has observed as safe; **physical reuse** occurs only when
the producer next writes a mapped slot. The serialized route prevents a later
acquisition across an owner hole. Shared claim plus ordered release allows
overlapping work but waits at the release cursor. Shared claim plus independent
slot completion lets later release calls return while producer reuse remains
contiguous. All three require cooperation after consumer acquisition.

A consumer dependency chain constrains a downstream consumer to advance only after its upstream
dependency. A fixed chain and an arbitrary runtime dependency graph are distinct mechanism scopes.

## Overflow and delivery semantics

- **Lossless backpressure** prevents publication when required capacity is unavailable.
- **Slowest-reader gating** prevents overwrite until every required reader has advanced.
- **Overwrite (lossy delivery)** permits newer publication to displace older data and requires
  detectable overruns.

Mechanisms need not provide identical semantics. A stale or overwritten entry must never be silently
interpreted as valid.

## Progress tracking

Candidate representations include head/tail indices, monotonic global sequences, per-slot
sequences, consumer gating sequences, and separate reservation/commit state. These terms are not
synonyms: documentation should name the state and the invariant it carries.

For concurrent producer claims, distinguish exclusive ownership of a bounded slot, completed payload
initialization, completion of the producer's publication call, a consumer-visible contiguous
publication frontier, consumer release, and physical slot reuse. Reservation order defines FIFO
position for shared ordered rings; payload and call completion may occur in a different order. A
claimed position is not necessarily ready or visible. A ready position after a hole is not
necessarily consumable under ordered delivery. A returned publication call need not imply that its
position is visible, if the contract explicitly represents out-of-order completion.

Out-of-order completion may be represented by a shared completion count, per-slot generation-tagged
availability, or another explicit state. A shared frontier may be advanced by producers or found by
the consumer. Completion representation and frontier owner are separate design dimensions. Charge
unfinished claims against bounded capacity; state whether release is the sole permission for reuse.
With producer-owned paths, define whether FIFO means per-producer order or a global merge order.
These choices also determine what a stalled claimant can block and which participant must poll or
help progress.

For concurrent consumer claims, distinguish the shared next-to-acquire cursor,
each owner's last slot access, independent completion metadata (if any), a
contiguous producer-visible reusable frontier (if any), and actual overwrite of
a physical slot. A shared claim cursor alone cannot authorize reuse: a claimed
consumer may still read the slot. A stalled or abandoned owner is a release
hole; later completion may be recorded without allowing the producer to cross
that slot at wrap. Per-slot generation state and a shared release frontier are
different representations, and helping the frontier is a separate choice from
marking completion.

The implemented completion-count route publishes only a whole completed claim
group: a newer unfinished claim can keep an earlier ready prefix invisible after
its hole closes. The implemented generation-tagged slot route lets the consumer
acquire-discover that prefix at its next position. Both charge claims until
consumer release and stop at a finite position limit; neither recovers an
abandoned owner. Their distinct publication state changes where contention occurs
and who discovers progress, while preserving shared-ring claim-order FIFO.

## Ownership and publication

Relevant operation shapes include:

- push/copy into queue-owned storage;
- direct write into queue-owned storage;
- `reserve -> write -> publish`;
- `consume -> release`.

Use these precise descriptions instead of loosely claiming `zero-copy`. Reservation and publication
may introduce obligations that a simple push operation does not have.

## Storage layout

- **Fixed inline slot**: each ring slot contains a fixed-capacity value or record.
- **Variable record byte ring**: aligned `[header][payload]` records occupy one circular byte buffer.
  The initial baseline should keep records physically contiguous and use a padding or wrap marker
  when the tail cannot fit a complete record.
- **Descriptor ring with separate payload storage**: compact metadata references bytes held in a
  distinct payload region. Consumers may inspect descriptors without touching payload.
- **Sequence-addressed metadata ring**: compact atomic fields and a publication sequence share each
  slot, permitting direct lookup and validated observation while newer publications overwrite old
  generations.
- **Chunk-addressed metadata/payload ring**: each publication slot names a separate fixed payload
  chunk whose reuse participates in the same validated overwrite lifecycle.

These layouts are conceptually distinct and should remain separate mechanism families.

## Payload model

Experiments may use fixed-size payloads, variable-size payloads, or mixed distributions. Useful
fixed sizes may include 8 B, 64 B, 256 B, and 1024 B, selected explicitly rather than swept
combinatorially by default.

## Publication granularity

Publication may occur per message or in a batch. Bulk APIs may also distinguish fixed-count success
from best-effort burst behavior; any such semantics must be named explicitly.

## Waiting strategy

Busy spin, CPU pause, yield, and later a justified blocking strategy are separate experimental
dimensions. Waiting behavior must not be hidden inside a mechanism comparison.

## Memory ordering

Each mechanism begins with a conservative correct baseline. Later variants may reduce
acquire/release operations or use relaxed ordering where an explicit happens-before argument and
tests support the change. C++ memory ordering must be reasoned about directly rather than copied
from another language or architecture.

## Workloads and dimensions

Fundamental workloads are steady-state throughput and ping-pong round-trip latency. Later workloads
may cover bursts, producer/consumer imbalance, temporary consumer stalls, and latency under offered
load.

The harness may grow toward these explicit dimensions:

- implementation;
- payload size;
- slot capacity or byte capacity;
- batch size;
- iteration count, warmup, and trial count;
- producer CPU and consumer CPU;
- fixed consumer count for broadcast workloads.

Not every option applies to every mechanism. Commands should reject nonsensical combinations rather
than force them through a universal configuration object. Default experiments should remain small;
broader sweeps should be explicit.
