# DPDK Ring Inspiration

## Purpose

DPDK's ring library is relevant for studying fixed-capacity FIFO rings, separate reservation and
publication state, and bulk/burst operations. `handoff` may reproduce selected algorithms without
pulling in EAL, mbufs, hugepages, or unrelated DPDK infrastructure.

## Relevant ideas

The classic ring design maintains separate producer and consumer head/tail state. Its documented
multi-producer path separates head reservation, object writes, and tail publication; classic shared
head reservation uses compare-and-swap. Producer and consumer synchronization modes can be selected
independently, including single-producer and single-consumer cases.

Bulk operations request a fixed count and fail if the complete request cannot be satisfied. Burst
operations process as many entries as currently possible. Selected SP/SC and HTS modes also expose
staged start/finish APIs; direct ring access at wrap may be represented by two contiguous spans.

Capacity semantics vary by configuration. A default power-of-two ring commonly leaves one element
unusable, while exact-size mode has different allocation rules. Any inspired implementation must
state its own usable capacity.

## Intentionally excluded

- API or ABI compatibility with `rte_ring`;
- EAL, mbufs, hugepage, allocator, and NUMA setup;
- the claim that every synchronization mode has the same CAS or waiting behavior;
- treating `lockless` as wait-free or preemptible;
- extending DPDK's `zero-copy API` name into an end-to-end no-copy claim.

## Possible use in handoff

Later work may isolate SP/SC head reservation, ordered tail publication, fixed-count bulk operations,
best-effort burst operations, and `reserve -> write -> finish` APIs. Multi-producer or multi-consumer
synchronization belongs after the simpler mechanism is understood.

## Primary sources

- [DPDK 25.11 Ring Library Programmer's Guide](https://doc.dpdk.org/guides-25.11/prog_guide/ring_lib.html)
- [`rte_ring` 25.11 API reference](https://doc.dpdk.org/api-25.11/rte__ring_8h.html)
- [Ring core definitions at a fixed revision](https://github.com/DPDK/dpdk/blob/d55ccd4e6de64e3f797f60de9e81f1d60f849775/lib/ring/rte_ring_core.h)
