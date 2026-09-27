# DPDK Ring Inspiration

## Purpose

DPDK's ring library is relevant for studying fixed-capacity FIFO rings, separate reservation and
publication state, and bulk/burst operations. `handoff` reproduces selected ideas without
pulling in EAL, mbufs, hugepages, or unrelated DPDK infrastructure.

## Relevant ideas

The classic ring design maintains separate producer and consumer head/tail state. Its documented
multi-producer path separates head reservation, object writes, and tail publication; classic shared
head reservation uses compare-and-swap. A finisher waits until the publication tail reaches its
reserved start, then release-stores the new tail. A delayed earlier producer can therefore block a
later finisher and the consumer's visible progress. Producer and consumer synchronization modes can
be selected independently, including single-producer and single-consumer cases.

RTS keeps concurrent reservations but adds head/tail update counts. Every finisher updates the tail
count; only the finisher whose count catches the head count advances its position. This avoids the
classic ordered tail wait in a finishing thread, while a missing earlier completion still prevents
the visible tail from advancing. It publishes a completed claim group, not the first contiguous
prefix of individually ready slots. If another claim is outstanding when a hole closes, the tail
can remain behind that hole until the newer claim also finishes. Its configurable head/tail distance
gate defaults to one eighth of capacity; the gate is distinct from physical capacity against the
consumer tail and should be specified deliberately in a controlled adaptation. HTS allows a new
claim only when head and tail match, serializing the whole operation; that is a different control
point from classic concurrent claims.

Bulk operations request a fixed count and fail if the complete request cannot be satisfied. Burst
operations process as many entries as currently possible. Selected SP/SC and HTS modes also expose
staged start/finish APIs; direct ring access at wrap may be represented by two contiguous spans.

Capacity semantics vary by configuration. A default power-of-two ring commonly leaves one element
unusable, while exact-size mode has different allocation rules. Any inspired implementation must
state its own usable capacity.

SORING extends ring ingress/egress with processing stages. A stage acquire grants exclusive worker
ownership; release marks a claimed range finished, and a contiguous stage frontier advances through
finished ranges. Release, a later stage, or the final consumer can help finalize earlier completed
ranges. This is a later composition of worker ownership and ordered stage progress, not a direct
extension of the existing single-owner `handoff` pipeline.

On the consumer side, classic multi-consumer dequeue CAS-reserves a unique head
range, copies the objects, then waits for the preceding consumer tail before
returning and making that range reusable to producers. RTS uses a consumer
head/tail completion count: a finisher can return without waiting for its
predecessor, while the tail position advances only when the count catches the
head count. A newer outstanding dequeue can therefore delay an already-finished
prefix. HTS admits one consumer transaction at a time by requiring consumer
head and tail to match. These are dequeue/copy lifetimes in DPDK; `handoff`'s
planned direct-slot work sharing keeps ownership until the worker's final slot
access, which can be substantially later than a dequeue copy.

## Intentionally excluded

- API or ABI compatibility with `rte_ring`;
- EAL, mbufs, hugepage, allocator, and NUMA setup;
- the claim that every synchronization mode has the same CAS or waiting behavior;
- treating `lockless` as wait-free or preemptible;
- extending DPDK's `zero-copy API` name into an end-to-end no-copy claim.

## Use in handoff

The implemented SP/SC bulk, burst, and staged rings already isolate those operation shapes. The
first multi-producer package studied claim order versus ordered tail publication,
with a serialized producer control. The completed producer program also studied an RTS-like
completion count. The completed consumer program used classic consumer head/tail, RTS, and HTS as
references for unique ownership, independent completion, and contiguous reuse; it did not copy
DPDK's dequeue/copy lifetime or distance gate. SORING is a primary reference for a future fixed
worker stage, not an implemented handoff mechanism or a reason to add a general stage runtime.

## Primary sources

- [DPDK 25.11 Ring Library Programmer's Guide](https://doc.dpdk.org/guides-25.11/prog_guide/ring_lib.html)
- [`rte_ring` 25.11 API reference](https://doc.dpdk.org/api-25.11/rte__ring_8h.html)
- [Ring core definitions at a fixed revision](https://github.com/DPDK/dpdk/blob/d55ccd4e6de64e3f797f60de9e81f1d60f849775/lib/ring/rte_ring_core.h)
- [Current classic producer and consumer head/tail implementation](https://github.com/DPDK/dpdk/blob/4f795ddd6a1fbdea97b7b254dea6d8f1f837a681/lib/ring/rte_ring_elem_pvt.h)
- [Current shared-head claim implementation](https://github.com/DPDK/dpdk/blob/4f795ddd6a1fbdea97b7b254dea6d8f1f837a681/lib/ring/rte_ring_c11_pvt.h)
- [Current RTS producer and consumer coordination](https://github.com/DPDK/dpdk/blob/4f795ddd6a1fbdea97b7b254dea6d8f1f837a681/lib/ring/rte_ring_rts_elem_pvt.h)
- [RTS mode contract and distance gate](https://github.com/DPDK/dpdk/blob/4f795ddd6a1fbdea97b7b254dea6d8f1f837a681/lib/ring/rte_ring_rts.h)
- [Current HTS coordination](https://github.com/DPDK/dpdk/blob/4f795ddd6a1fbdea97b7b254dea6d8f1f837a681/lib/ring/rte_ring_hts.h)
- [Current HTS consumer head/tail implementation](https://github.com/DPDK/dpdk/blob/4f795ddd6a1fbdea97b7b254dea6d8f1f837a681/lib/ring/rte_ring_hts_elem_pvt.h)
- [Current SORING ordered-stage implementation](https://github.com/DPDK/dpdk/blob/4f795ddd6a1fbdea97b7b254dea6d8f1f837a681/lib/ring/soring.c)
