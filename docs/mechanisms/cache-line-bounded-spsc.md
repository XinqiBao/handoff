# Cache-line-separated bounded SPSC ring

## Purpose and isolated difference

`CacheLineBoundedRing<T, Capacity>` preserves the basic bounded SPSC mechanism while changing one
structural property: the consumer-owned `head` and producer-owned `tail` counters occupy distinct
128-byte-aligned state blocks. The basic implementation leaves those atomics adjacent.

The 128-byte value is an explicit conservative layout choice that separates the counters on systems
with common 64-byte or 128-byte cache lines. It avoids toolchain-dependent interference-size
constants. This layout does not discover the host cache-line size and is not a claim about every
architecture.

## Preserved behavior

Capacity is compile-time and exactly usable. The ring contains `Capacity` default-constructed inline
slots and uses the same wrapping unsigned counters, modulo slot addressing,
non-blocking `try_push` and `try_pop` operations, and type/lifetime requirements as the
[basic bounded SPSC ring](basic-bounded-spsc.md). The ring allocates no slot storage dynamically,
but each slot remains a live `T` for the ring's lifetime, so `T` construction and assignment may
allocate, release, or retain resources. Pop move-assigns from a slot without ending that slot's
lifetime.

Producer and consumer ownership, full and empty conditions, FIFO behavior, and acquire/release
publication are unchanged. Both operations still read the remote counter on every attempt. The
variant does not cache remote indices, batch publication, weaken memory ordering, change payload
placement, or add a waiting strategy.

Capacity must be a positive power of two and at most half the counter range. The
[basic ring note](basic-bounded-spsc.md#representation-and-capacity) explains why bounded unsigned
distance alone does not preserve modulo slot mapping at machine-counter rollover. The same contract
and [production-counter boundary tests](../../tests/counter_rollover_test.cpp) apply here.

## Expected effect and limits

Separating the counters is intended to reduce cache-line interference when producer and consumer
run concurrently on different cores. The larger object and stronger alignment may alter storage
footprint and placement, and the inline slot array can still share cache sets with state. Those are
remaining confounders, not additional intended mechanisms.

The common bounded-FIFO and payload/lifetime suite runs unchanged against both implementations,
including move-only resource ownership, failed-operation preservation, slot reuse, and the
million-message integrity test. Layout-specific compile-time checks verify the advertised alignment.

## Evidence and limits

[Experiment 003](../experiments/003-cache-line-spsc-comparison.md) recorded throughput and RTT
improvements for the separated layout on its historical Intel N150 configuration and revision.
Those are observations of the complete routes; object size, placement, code generation, and cache
mapping prevent attribution to coherence alone.

The later [throughput reassessment](../experiments/023-spsc-measurement-stability.md) found
persistent process rate states at the 8 B / 64-slot scalar shape, so small current throughput gaps
remain unresolved. The [RTT reassessment](../experiments/030-spsc-rtt-repeatability.md) supports a
conditional tail direction while leaving the small median gap unresolved. Read those limits with
the historical comparison; the earlier numbers are not a current ranking or an enduring effect
size. The power-of-two capacity restriction leaves the representation at those measured shapes
unchanged.
