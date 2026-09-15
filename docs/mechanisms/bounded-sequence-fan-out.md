# Bounded sequence fan-out

## Purpose and isolated difference

`BoundedSequenceFanOut<T, Capacity, ConsumerCount>` extends sequence-addressed publication to one
producer and a fixed compile-time set of reliable consumers. Every consumer observes every
published sequence independently, and producer reuse is gated by the slowest consumer. The Q1
`BoundedSequenceRing` remains unchanged as the one-producer/one-consumer baseline.

The mechanism isolates Disruptor-inspired fan-out and gating sequences. It is not an LMAX API or
Java port and does not add sequence barriers, consumer dependency graphs, dynamic registration,
reader removal, multiple producers, work sharing, or lossy overwrite.

## Representation and finite sequence domain

The ring owns `Capacity` default-constructed inline `T` slots. Sequence `n` maps to physical slot
`(n - 1) % Capacity`, every slot is usable, and sequences start at one. `ConsumerCount` is fixed by
the type and must be at least two. Each index in `[0, ConsumerCount)` permanently identifies one
consumer; an invalid index throws `std::out_of_range`.

The producer owns a non-atomic next-to-claim sequence and active/exhausted flags. A single atomic
producer cursor records the highest contiguous published sequence. Each indexed consumer owns its
non-atomic next-to-observe sequence and active/exhausted flags, while its atomic gating sequence
records the highest sequence it has fully released. All cursors begin at zero.

The default `std::uint64_t` sequence domain ends at `UINT64_MAX` and never wraps. Publishing the
limit exhausts the producer; each consumer independently becomes exhausted after releasing that
limit. The optional unsigned sequence type permits bounded tests with smaller domains. Capacity
must fit in the sequence type.

Slots remain alive until ring destruction and are reused through caller assignment. Observation
returns `const T&`; release neither moves from nor destroys a slot. Resources remain retained after
all consumers release and are discarded only when a later producer assignment overwrites the slot.
`T` must be default-initializable.

## Claim and observation contract

`try_claim()` returns a move-only token only when the producer is not exhausted, no producer claim
is outstanding, and the next sequence would not exceed exact capacity relative to the minimum of
all consumer gating sequences. The token exposes the sequence and mutable slot. `publish()` makes
the populated slot visible. `cancel()` or destruction publishes nothing and lets the producer retry
the same sequence.

`try_observe(index)` returns a move-only token when that consumer has no outstanding observation,
is not exhausted, and the producer cursor has reached its next sequence. The token exposes its
stable consumer index, sequence, and const slot value. `release()` advances only that consumer's
gating sequence. `cancel()` or destruction leaves only that consumer's progress unchanged, so it
retries the same sequence; other consumers remain independent.

Move construction transfers responsibility and deactivates the source. Move assignment first
cancels a token already held by the destination. Publishing, releasing, or cancelling an inactive
token is harmless; accessing its value is outside the contract. The producer and every indexed
consumer are each confined to their declared owner thread, tokens do not cross owner threads, and
the ring outlives every token.

## Capacity and memory ordering

The producer scans every atomic consumer gating sequence with acquire loads and takes their
minimum. Occupancy is `(next_to_claim - 1) - minimum_gating`. A claim is refused when occupancy is
at least `Capacity`, so a fast consumer cannot hide a slow consumer or make unread storage reusable.
A stale gating load can only preserve an older, smaller minimum and reject a claim conservatively.

Producer slot writes are sequenced before the release-store that publishes the producer cursor.
Each consumer acquire-loads that cursor before observing a slot, so a load that sees the published
sequence synchronizes with publication and exposes the slot writes to that consumer.

Each consumer's slot reads are sequenced before its release-store to its own gating sequence. The
producer may reuse a slot only after its acquire scan observes sufficiently advanced gating values
from every consumer. Each observed release therefore makes that consumer's completed reads happen
before the producer's subsequent overwrite. The producer cursor is contiguous because claims are
single-producer and one-at-a-time; each consumer's progress is contiguous because it observes its
own next sequence and holds at most one token. Per-slot availability state is not needed.

Consumer states are stored directly without cache-line padding. This keeps Q2 focused on consumer
count and slowest-reader gating; any later state-placement variant must remain a separate
experiment.

## Correctness and benchmark coverage

Tests cover initial state, stable and invalid indices, all-consumer delivery, independent progress,
exact capacity, physical wrap, fast-consumer wrap attempts, slowest-reader backpressure, nested
rejection, producer and per-consumer cancellation, move-only tokens, finite-limit rejection,
resource retention, and a long concurrent run with two differently paced consumers.

The `fan-out` throughput implementation instantiates two consumers. Warmup completes only after the
producer and both consumers finish their warmup phases. The timed phase stops after both consumers
validate and release every publication. `iterations` and `messages_per_second` count publications,
not the sum of two deliveries; the stored checksum is emitted only after both independent consumer
checksums match the same expected value. Controlled runs accept a producer CPU and an ordered pair
for consumer 0 and consumer 1. Linux verifies each effective mask and result metadata records every
role. Unpinned smoke runs remain plumbing evidence only.

## Provenance

Independent consumer sequences and slowest-reader producer gating come from the repository's
[LMAX Disruptor inspiration note](../inspirations/lmax-disruptor.md). This implementation supplies
its own C++ ownership and acquire/release argument and makes no compatibility claim.

## Measured observation

On the controlled Intel N150 configuration recorded in
[experiment 009](../experiments/009-sequence-fan-out-comparison.md), two-consumer fan-out completed
43.389% fewer publications per second at the pooled median than the single-consumer sequence route.
All three paired blocks agreed, although fan-out throughput drifted upward and the exact magnitude
was variable. This is the conditional cost of a complete richer contract: fan-out performs two
reliable deliveries and validations, uses an additional worker, and scans two gating sequences.
It is not an isolated consumer-count, atomic, minimum-scan, topology, or LMAX cost.
