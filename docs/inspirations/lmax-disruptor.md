# LMAX Disruptor Inspiration

## Purpose

The Disruptor is relevant as a source of ideas about sequence-based coordination over preallocated
event storage. `handoff` may reproduce selected mechanisms in small C++ implementations; it does not
plan to port the Java API or claim compatibility.

## Relevant ideas

The current Disruptor design separates event storage from concurrency coordination. A `Sequencer`
coordinates producers and consumers around monotonic `Sequence` values, while sequence barriers
express availability and consumer dependencies. Gating sequences prevent producers from wrapping
over entries that required consumers have not yet processed.

Low-level publication follows a claim, populate, then publish protocol. Independent consumers can
observe each published event, and dependency graphs can gate downstream consumers. Waiting strategy
is a separate choice; the system should not be described generically as lock-free because a blocking
wait strategy uses a lock and condition.

## Intentionally excluded

- the complete Java API, DSL, event-factory model, and handler lifecycle;
- direct translation of Java memory semantics into C++ memory orders;
- historical benchmark figures as current cross-platform evidence;
- an unqualified `zero-copy` or lock-free claim.

## Possible use in handoff

Later mechanism-isolation work may study sequence claiming, publication, producer cursors, consumer
gating, independent readers, dependency graphs, and eventually multi-producer availability tracking.
Each C++ mechanism needs its own memory-model argument and correctness tests.

## Primary sources

- [Official Disruptor user guide](https://lmax-exchange.github.io/disruptor/user-guide/)
- [Sequencer API at a fixed revision](https://github.com/LMAX-Exchange/disruptor/blob/c871ca49826a6be7ada6957f6fbafcfecf7b1f87/src/main/java/com/lmax/disruptor/Sequencer.java)
- [Original technical paper](https://lmax-exchange.github.io/disruptor/files/Disruptor-1.0.pdf)
