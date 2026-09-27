# Completion-count MPSC ring

`CompletionCountRing<T, Capacity, Sequence>` asks whether a producer can finish its
publication call across an earlier unfinished claim. It adapts the completed-group
idea of DPDK RTS to one consumer and exact bounded inline slots. It is not a DPDK
implementation or compatible with its API, distance gate, or memory model.

CAS on `next_claim` grants one unique position and mutable slot to a producer.
`next_claim - released < Capacity` charges unfinished, ready, and visible but
unreleased claims equally. The producer must finish its payload write and call
`publish()` exactly once. There is no cancellation or abandoned-owner recovery;
destruction of an active claim terminates. All slots are live, default-constructed
`T` objects throughout ring lifetime. The ring must outlive all tokens and threads.

`publish()` increments `completed` with an acquire-release RMW and returns without
waiting for its predecessor. If the resulting count equals a subsequent snapshot of
`next_claim`, that finisher advances `published` to the snapshot by a release CAS.
The CAS only moves `published` forward: a finisher delayed after catching an older
group cannot regress a newer tail. The consumer acquire-checks `published`, reads
one position in claim order, then release-stores `released` after its final read.
Only that release permits physical slot reuse. An observation may be cancelled and
retried; the consumer holds at most one observation.

The group tail can lag a ready prefix. If position 0 is held, 1 finishes, and 2
is claimed but unfinished before 0 finishes, the completion count is 2 while the
claim count is 3. Position 0 and 1 remain invisible until 2 finishes. A stalled
owner blocks consumer progress at or before its group, and capacity eventually
fills, but later producers can return from publication while space remains. A
stalled producer after its completion RMW but before tail advancement can also
delay visibility until another catching finisher advances it. There is no
operation-wide lock-free or wait-free guarantee: claim CAS can starve, a stale
release read can conservatively report full, and group visibility depends on
participating finishers. Harness retries yield.

The acquire part of each completion RMW reads the preceding RMW's release sequence.
It therefore carries every earlier finisher's payload write and claim CAS to a
catching finisher. The release CAS on `published` carries those writes to the
consumer's acquire load. The consumer's release of `released`, followed by a
claimant's acquire load, orders the last read of a physical slot before reuse.
The claim CAS is relaxed because uniqueness comes from its modification order;
the release cursor supplies the lifetime edge. A catching finisher's relaxed
claim-count load sees at least all claims whose completions happen-before it.

Logical positions and completion counts start at zero and never wrap. The last
claimable position is `max(Sequence) - 1`; its one-past count is representable.
Further claims fail even after release. This rules out generation ambiguity and
finite-counter ABA. Each slot has one owner until publication and one consumer
until release. Control atomics are separate members but not cache-line isolated;
shared claim and completion lines may contend, and payload slots can share lines.
The layout intentionally leaves false-sharing effects in the complete-route
comparison rather than assuming an isolated atomic cost.

Deterministic tests cover call return across a hole, group-tail lag after closing
it, full capacity, release and wrap, finite exhaustion, and concurrent integrity.
The common publication-hole diagnostic and scalar MPSC throughput workload also
exercise this route. Passing executions and sanitizers support the exercised
paths; the argument above supplies the C++ synchronization basis.
