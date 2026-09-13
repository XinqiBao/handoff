# Cached-index bounded SPSC ring

## Purpose and isolated difference

`CachedIndexBoundedRing<T, Capacity>` is a fixed-slot SPSC variant that changes one behavior from
the basic ring: each thread retains the last remote progress value it acquired. The producer avoids
loading the consumer's `head` while its cached head still proves space is available, and the
consumer avoids loading the producer's `tail` while its cached tail still proves an item is
available.

This is a mechanism-isolation variant, not a general queue abstraction. It preserves the basic
ring's API, exact usable capacity, inline slot storage, lossless backpressure, payload requirements,
and one-producer/one-consumer contract. It does not add cache-line alignment, batching,
reservations, blocking waits, or weaker memory ordering.

## Representation and ownership

The ring contains `Capacity` default-constructed `T` slots, atomic monotonic `head` and `tail`
counters, a producer-owned cached head, and a consumer-owned cached tail. All four counters start at
zero. Slots remain live for the ring's lifetime and are reused by assignment, with the same
resource-allocation and moved-from behavior documented for the
[basic bounded SPSC ring](basic-bounded-spsc.md).

Only the producer reads and writes its cached head and advances `tail`. Only the consumer reads and
writes its cached tail and advances `head`. The cached members are therefore non-atomic. They are
the mechanism's necessary additional state; the atomic counters remain unaligned as in the basic
ring so this variant does not also study explicit cache-line separation.

## Operations and invariants

The producer first compares its current tail with its cached head. If their distance equals
`Capacity`, the cached value makes the ring appear full, so the producer acquires the current head
and checks again. It reports full only if that refreshed distance is still `Capacity`. Otherwise it
assigns the next slot and release-stores the advanced tail.

The consumer first compares its current head with its cached tail. If they are equal, the cached
value makes the ring appear empty, so the consumer acquires the current tail and checks again. It
reports empty only if the refreshed values are still equal. Otherwise it move-assigns the next slot
into the output and release-stores the advanced head.

A stale cached head can only make the producer underestimate free space; it cannot authorize an
overwrite. A stale cached tail can only make the consumer underestimate available items; it cannot
authorize reading unpublished storage. Failed operations change neither ring state nor caller
values. Unsigned distance and counter-wrap constraints are unchanged from the basic ring.

## Memory ordering

Producer and consumer loads of their own published counters remain relaxed. When the producer
refreshes its cached head, the acquire load synchronizes with the consumer's release publication of
head after that consumer has finished reading the corresponding slots. The acquired bound remains
safe while the producer reuses those released slots in order.

When the consumer refreshes its cached tail, the acquire load synchronizes with the producer's
release publication of tail after assigning the corresponding slot. Because producer operations
are sequenced, acquiring a later tail also makes all earlier published slot assignments visible.
The consumer may safely read through that acquired bound without another shared tail load.

Caching changes when acquire loads occur, not the release/acquire publication argument. Relaxing
those orders would be a separate experiment.

## Correctness and benchmark coverage

The shared bounded-FIFO, wraparound, payload/lifetime, and concurrent integrity suite applies to
this ring. A focused deterministic test repeatedly fills and drains a small ring so both stale
remote caches must refresh across slot wraparound.

The benchmark CLI names the variant `cached-index` and dispatches it through the same throughput
and ping-pong workload templates as the existing rings. Small runs validate plumbing only. The
planned comparison is recorded separately and requires controlled Linux evidence before any
performance conclusion.
