# Ordered worker-stage ring

`OrderedWorkerRing<T, Capacity, Sequence>` studies one producer, competing workers in one fixed
processing stage, and one ordered downstream consumer. It owns exactly `Capacity` live,
default-constructed inline slots. A worker has mutable direct access to its uniquely acquired slot;
the downstream consumer has const access after that worker completes. The intended first workload
uses two workers and a small deterministic in-place transformation. The API does not identify
workers or enforce one claim per worker. The ring must outlive all tokens and threads.

## Lifecycle and operations

Logical positions start at zero. The sole producer holds at most one `ProducerClaim`, populates its
slot, then calls `publish()`. `cancel()` abandons an unpublished claim so the same position can be
retried. Publication release-stores the one-past position. Workers acquire that publication cursor
and CAS a shared `next_worker` cursor for unique positions. Each `WorkerClaim` allows mutable slot
access through the final worker operation; `complete()` release-stores `position + 1` in that
physical slot's completion tag and returns without waiting for an earlier worker. Worker claims
cannot be cancelled or transferred to another owner after acquisition. An active worker token's
destruction terminates.

The sole downstream consumer scans exact-generation completion tags from its private
`completed_prefix` cursor. `completed_prefix()` returns the one-past contiguous stage-complete
position; a mismatch ends discovery. `try_acquire_downstream()` grants only its next ordered
position below that prefix and holds at most one `DownstreamClaim`. The consumer reads the completed
slot and calls `release()` after its last access. An active downstream token's destruction
terminates; there is no downstream cancellation or replay. A worker completion-call return can
precede stage visibility when an earlier worker leaves a hole. Once that hole closes, the
downstream consumer can discover already-completed successors without another worker action.

These are different lifecycle boundaries: reusable slot, producer-owned slot, published slot,
uniquely worker-owned slot, worker-complete slot, contiguous downstream-visible slot,
downstream-owned slot, downstream-released slot, then physical reuse on a later producer write.
The producer may claim only when `next_publish - released < Capacity`. Thus worker completion and
stage visibility do not free capacity; only the ordered downstream release cursor does. Release
authorizes reuse, while the next producer assignment actually reuses the physical slot. A later
slot can be reused after its own downstream release even while a still later downstream claim is
held. Occupancy is exact, including at physical wrap.

The final claimable position is `max(Sequence) - 1`, leaving its one-past tag representable.
Logical positions never roll over; `try_claim()` rejects further claims at that limit. The
completion tag for one physical generation differs from every prior generation's one-past value.
Slots and their retained resources stay alive until ring destruction and are overwritten by later
producer assignment, not destroyed at release.

## C++ ordering and ownership

Producer writes precede the release store to `published`. Every successful worker claim has first
acquire-loaded a publication cursor beyond its claimed position. The claim CAS grants that position
to one worker; the acquire load makes producer writes visible before worker slot access. The worker
finishes all slot reads and writes before its release store of the exact completion tag. The
downstream consumer's matching acquire load makes that worker's transformation and the preceding
producer writes visible. Its private prefix advances only across matching tags, so it cannot grant
a position across an unfinished earlier worker. The `published` bound prevents scanning positions
that were never published.

The downstream consumer's last slot access precedes its release store to `released`. The producer
acquire-loads `released` before claiming enough capacity to wrap to that same physical slot; its
next write is therefore ordered after the downstream access. A stale producer load merely rejects
a claim. Before a downstream release, no producer can reach that physical slot's next generation;
thus the matching completion tag cannot be overwritten while it is needed for downstream access.
After release, downstream never scans that generation again. Exact tag equality and finite logical
positions exclude stale-generation acceptance. Consecutive producer publication stores and
downstream release stores also preserve their respective single-owner order.

The producer, downstream consumer, and each worker must use their claims only on their respective
owner threads. The producer and downstream private cursors are single-thread confined; workers
coordinate only through atomics. The API requires cooperative completion and release. An abandoned
worker leaves a permanent completion hole, eventually blocking bounded progress. CAS retry can
starve. No fairness, participant-failure recovery, exactly-once external effects, lock-free or
wait-free operation-wide guarantee is established.

## Evidence and provenance

Deterministic tests hold an early worker while a later completion returns, close the hole with a
newer position unfinished, inspect final-release gating at exact capacity, and check stale tags
after physical wrap and finite `uint8_t` exhaustion. A two-worker concurrent test checks unique
ownership, ordered downstream position and transformed payload integrity over repeated wrap.
[Experiment 018](../experiments/018-ordered-worker-stage.md) records the validation evidence.

The fixed [pipeline](bounded-sequence-pipeline.md) supplies the final-stage reuse control; the
[SPMC slot-completion ring](slot-completion-spmc.md) supplies the opposite-side prefix discovery
comparison. [DPDK SORING](../inspirations/dpdk-ring.md) and the
[Disruptor dependency model](../inspirations/lmax-disruptor.md) informed the question. This ring
does not implement their APIs, helping, lifecycle, or memory models.
