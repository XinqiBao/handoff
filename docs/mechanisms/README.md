# Mechanism Notes

This directory documents mechanisms implemented by `handoff`.

- [Basic bounded SPSC ring](basic-bounded-spsc.md)
- [Cache-line-separated bounded SPSC ring](cache-line-bounded-spsc.md)
- [Cached-index bounded SPSC ring](cached-index-bounded-spsc.md)
- [Batch bounded SPSC ring](batch-bounded-spsc.md)
- [Bulk/burst bounded SPSC ring](bulk-burst-bounded-spsc.md)
- [Staged bounded SPSC ring](staged-bounded-spsc.md)
- [Bounded sequence ring](bounded-sequence-ring.md)
- [Bounded sequence fan-out](bounded-sequence-fan-out.md)
- [Bounded sequence pipeline](bounded-sequence-pipeline.md)

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
