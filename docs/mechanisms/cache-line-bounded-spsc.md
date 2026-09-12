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
slots and uses the same monotonically increasing unsigned counters, modulo slot addressing,
non-blocking `try_push` and `try_pop` operations, and type/lifetime requirements as the
[basic bounded SPSC ring](basic-bounded-spsc.md).

Producer and consumer ownership, full and empty conditions, FIFO behavior, and acquire/release
publication are unchanged. Both operations still read the remote counter on every attempt. The
variant does not cache remote indices, batch publication, weaken memory ordering, change payload
placement, or add a waiting strategy.

## Expected effect and limits

Separating the counters is intended to reduce cache-line interference when producer and consumer
run concurrently on different cores. The larger object and stronger alignment may alter storage
footprint and placement, and the inline slot array can still share cache sets with state. Those are
remaining confounders, not additional intended mechanisms.

The common bounded-FIFO suite runs unchanged against both implementations, including the
million-message integrity test. Layout-specific compile-time checks verify the advertised alignment.
