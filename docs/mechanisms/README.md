# Mechanism Notes

This directory documents mechanisms implemented by `handoff`.

## Point-to-point SPSC and publication variants

- [Basic bounded SPSC ring](basic-bounded-spsc.md)
- [Cache-line-separated bounded SPSC ring](cache-line-bounded-spsc.md)
- [Cached-index bounded SPSC ring](cached-index-bounded-spsc.md)
- [Batch bounded SPSC ring](batch-bounded-spsc.md)
- [Bulk/burst bounded SPSC ring](bulk-burst-bounded-spsc.md)
- [Staged bounded SPSC ring](staged-bounded-spsc.md)
- [Bounded sequence ring](bounded-sequence-ring.md)

## Producer coordination

- [Two producer-owned paths and one merge consumer](two-path-merge.md)
- [Ordered-publication MPSC ring](ordered-publication-mpsc.md)
- [Completion-count MPSC ring](completion-count-mpsc.md)
- [Slot-availability MPSC ring](slot-availability-mpsc.md)

## Consumer coordination

- [Serialized-consumer SPMC control](serialized-consumer-spmc.md)
- [Ordered-release SPMC ring](ordered-release-spmc.md)
- [Per-slot completion SPMC ring](slot-completion-spmc.md)

## Broadcast and dependencies

- [Bounded sequence fan-out](bounded-sequence-fan-out.md)
- [Bounded sequence pipeline](bounded-sequence-pipeline.md)
- [Ordered worker-stage ring](ordered-worker-stage.md)

## Record and storage layouts

- [Fixed-record SPSC ring](fixed-record-spsc.md)
- [Variable-record SPSC byte ring](variable-record-spsc.md)
- [Descriptor/payload SPSC ring](descriptor-payload-spsc.md)
- [Sequence-addressed metadata ring](sequence-metadata-ring.md)
- [Sequence-addressed metadata/payload ring](sequence-payload-ring.md)

The serialized two-producer route is a benchmark control using the basic SPSC ring under a producer
mutex; it is not a separate mechanism implementation. Its comparison is recorded in
[Experiment 015](../experiments/015-mpsc-ordered-publication.md).

Each future note should state:

- purpose and experiment class;
- producer/consumer and delivery contract;
- representation, ownership, and capacity semantics;
- operations and publication lifecycle;
- invariants and memory-order reasoning;
- full, empty, waiting, and overflow behavior;
- test coverage and known limitations;
- relationship to other variants and any external inspiration.

A note describes this repository's actual behavior. External systems and provenance belong under
[`docs/inspirations`](../inspirations/README.md).
