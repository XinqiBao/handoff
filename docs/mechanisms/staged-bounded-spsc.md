# Staged bounded SPSC ring

## Purpose and isolated difference

`StagedBoundedRing<T, Capacity>` separates fixed-slot reservation from publication and release in a
single-producer/single-consumer ring. A producer reserves an exact group, writes through up to two
spans, and explicitly finishes to publish it. A consumer reserves an exact published group, reads
through up to two const spans, and explicitly finishes before those slots may be reused.

This staged lifecycle and physical-wrap representation are inspired by DPDK ring start/finish and
direct-access APIs. The implementation is not compatible with DPDK API or ABI and does not include
EAL, mbufs, hugepages, allocator integration, synchronization modes, head CAS, multi-producer or
multi-consumer behavior, or an end-to-end no-copy claim.

## Representation and payload lifetime

The ring owns `Capacity` default-constructed inline `T` slots and monotonic atomic head and tail
counters. All `Capacity` slots are usable. Logical positions map to physical slots modulo
`Capacity`. Slots remain alive until the ring is destroyed and retain resources after consumer
finish until a later producer write replaces them.

The ring itself requires only default initialization of `T`. The caller chooses how to modify a
producer span and therefore supplies any assignment or mutation operation needed by that code. A
consumer reservation exposes `std::span<const T>` so reading cannot silently become destructive
movement from a published slot.

## Reservation contract

`try_reserve_push(count)` and `try_reserve_pop(count)` are all-or-nothing. Zero, a count larger than
`Capacity`, insufficient current space or data, or another outstanding reservation on the same side
returns an empty `std::optional`. A success returns a move-only token whose `first()` and `second()`
spans have a combined size exactly equal to the positive request. The second span is empty when the
logical range does not wrap.

Only the producer may hold and operate producer tokens; only the consumer may hold and operate
consumer tokens. A side must finish or cancel its token before reserving again. Tokens are not
copyable. Moving transfers responsibility and deactivates the source. Move assignment first cancels
the destination's current reservation. `cancel()` and destruction abandon the reservation without
advancing a shared counter; `finish()` advances the exact reserved count once. Calls on an inactive
token are harmless, and its span accessors return empty spans.

The caller must keep the ring alive until every token referring to it has been finished, cancelled,
or destroyed. Moving a token to another thread is outside the supported ownership contract.
Exceptions while caller code writes or reads a span do not publish or release the reservation when
stack unwinding destroys the token.

Producer cancellation may leave newly assigned values in free slots, but they remain unpublished
and a later producer reservation may overwrite them. Consumer cancellation leaves head unchanged,
so the producer cannot reuse the reserved occupied slots.

## Publication and memory ordering

Producer reservation relaxed-loads its owned tail and acquire-loads head before granting free slots.
Producer `finish()` release-stores the advanced tail after all caller writes sequenced before it. A
consumer acquire-load that grants the corresponding range observes those writes.

Consumer reservation relaxed-loads its owned head and acquire-loads tail before exposing const
slots. Consumer `finish()` release-stores the advanced head after all caller reads sequenced before
it. A producer acquire-load cannot grant those slots for reuse until the reads are complete.
Cancellation changes only the side-local outstanding-reservation flag and publishes no counter.

The side-local flags are non-atomic because their methods and tokens belong exclusively to their
declared owner thread. Violating the SP/SC ownership contract is unsupported.

## Correctness and benchmark coverage

Mechanism-specific tests cover zero, oversized, insufficient, full, exact-boundary, and wrapped
reservations; span sizes and FIFO order; visibility only after producer finish; reuse only after
consumer finish; nested rejection; explicit and destructor cancellation; token moves and move
replacement; resource retention; and long concurrent integrity with a capacity that forces both
physical spans.

The `staged` throughput mode reserves the configured group size, generates payloads directly into
the producer spans, validates them directly from the consumer spans, and then finishes each side.
Iterations, checksum, and messages per second still describe completed handoffs. This removes the
benchmark's intermediate payload-array-to-slot and slot-to-payload-array assignments, but does not
imply that payload generation, assignment into slots, observation, or unrelated system copies have
disappeared. Ping-pong rejects the staged mode because it does not study grouped reservation.

## Provenance

The staged lifecycle and two-span wrap representation come from the DPDK ring ideas recorded in the
repository's [DPDK inspiration note](../inspirations/dpdk-ring.md). This experiment retains only the
small SP/SC mechanism needed for local study.

## Measured observation

On the controlled Intel N150 configuration recorded in
[experiment 007](../experiments/007-staged-spsc-comparison.md), staged direct span access completed
4.796% more messages per second at group size 16 than the equal all-or-nothing bulk route. All three
paired blocks agreed and staged had a 0.157% sample CV. This is evidence for the complete staged
route under one measured workload, not an end-to-end no-copy, DPDK compatibility, or isolated-copy
claim.

A later selective worker-only PMU follow-up reproduced a 4.966% throughput advantage. Staged
retired 5.041% more instructions per second, while its approximate instructions/message differed by
only +0.134% and did not have a consistent block direction. Approximate cycles/message were 4.474%
lower and IPC was 4.888% higher, both with consistent paired directions. This weakens the hypothesis
that fewer retired instructions per completed message explains the advantage, but does not isolate
which complete-route or compiler difference improves execution efficiency.
