# Mechanism Notes

This directory documents mechanisms implemented by `handoff`. There are no substantive mechanisms
in the bootstrap state.

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
