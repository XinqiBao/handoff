# Bulk/burst bounded SPSC ring

## Purpose and isolated difference

`BulkBurstBoundedRing<T, Capacity>` is a fixed-slot SP/SC variant that contrasts two group
progress contracts inspired by DPDK's ring library. Bulk operations require the complete requested
count to fit or be available. Burst operations process the currently possible prefix and return the
exact number completed.

This mechanism reproduces only that semantic contrast. It is not compatible with DPDK APIs or ABI
and does not include EAL, mbufs, hugepages, allocator integration, selectable synchronization
modes, staged reservation, cached indices, or cache-line-separated counters.

## Representation and payload lifetime

The ring owns `Capacity` default-constructed inline `T` slots and monotonic atomic head and tail
counters. Logical positions map to physical slots modulo `Capacity`; all `Capacity` slots are
usable. The producer owns tail advancement and the consumer owns head advancement. Exactly one
producer may call push operations and exactly one consumer may call pop operations.

Slots remain live for the ring's lifetime and are reused by assignment. Scalar operations preserve
the basic ring's copy/move behavior and support default-initializable move-only values. Group push
operations take `std::span<const T>` and require copy assignment. Group pop operations take
`std::span<T>`, require move assignment, and replace only the output prefix reported as completed.
The ring does not allocate slot storage, but `T` construction or assignment may manage resources.

## Bulk and burst semantics

`try_push_bulk` and `try_pop_bulk` return `bool`. A zero-length bulk succeeds without touching ring
state. A request larger than `Capacity` fails. A non-empty bulk succeeds only when the full span can
be transferred; insufficient space or data leaves counters, slots visible to the other thread, and
caller-owned input or output values unchanged. Successful transfer preserves FIFO order across a
physical wrap.

`try_push_burst` and `try_pop_burst` return a count. They process exactly
`min(requested, currently available)` elements, including when the requested span is larger than
`Capacity`. Zero-length requests and requests made against a full producer side or empty consumer
side return zero without publication. A partial pop changes only the completed output prefix; its
suffix remains untouched. Push inputs are never consumed or modified.

Bulk all-or-nothing behavior refers to the capacity or availability decision. Neither operation
provides transactional rollback if assignment of `T` throws.

## Publication and memory ordering

The producer relaxed-loads its owned tail and acquire-loads head. After assigning the elements
selected by a successful bulk or non-empty burst, it release-stores the advanced tail exactly once.
A consumer acquire-load of that tail observes all preceding slot assignments.

The consumer relaxed-loads its owned head and acquire-loads tail. After moving the elements selected
by a successful bulk or non-empty burst, it release-stores the advanced head exactly once. A
producer acquire-load of that head cannot reuse those slots before the consumer's reads complete.
Empty, full, and zero-length operations do not publish a counter.

## Correctness and benchmark coverage

The shared scalar capacity, FIFO, wraparound, payload lifetime, and concurrent integrity contracts
apply. Mechanism-specific tests cover zero, exact, partial, oversized, insufficient, and physically
wrapped transfers; unchanged bulk failure outputs and burst suffixes; resource-owning values; and
concurrent bulk and burst integrity.

The throughput benchmark exposes `bulk` and `burst` modes with requested group sizes 1, 4, and 16.
Both keep requesting any uncompleted suffix until every configured message has reached the
consumer. Therefore `iterations`, checksum validation, and messages per second describe completed
handoffs, not burst attempts or requested counts. Ping-pong does not exercise these group semantics
and rejects both modes.

The bulk route also serves as the equal all-or-nothing baseline for the direct-slot comparison in
[experiment 007](../experiments/007-staged-spsc-comparison.md). That experiment measured bulk versus
staged at group size 16; it did not measure burst calls or answer the partial-progress question.

## Provenance

The fixed-count bulk versus best-effort burst distinction comes from DPDK's ring library as recorded
in the repository's [DPDK inspiration note](../inspirations/dpdk-ring.md). Names and semantics here
are intentionally smaller and SP/SC-only.
