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

The current multi-producer sequencer atomically claims from a shared cursor and marks each slot's
generation-tagged availability in a separate flag array on publish. The cursor can include claimed
but unpublished entries; the consumer sequence barrier scans from its next required sequence to
find the highest contiguous available entry. Gating prevents reuse across wrap before required
consumers release the old generation. The upstream claim cursor advances before its capacity-gating
wait, so a bounded C++ adaptation may use a capacity-checked claim instead. This is conceptually
distinct from one cooperative producer tail that already names the contiguous frontier.

The historical 3.4.4 `WorkerPool` uses one shared `workSequence` CAS so a work
item has one worker, and gates producer wrap with the workers' individual
sequences. `WorkProcessor` records its prior sequence before claiming another;
its worker callback completes before it proceeds. This is a useful work-sharing
ownership model, but its handler, exception, release-aware callback, worker
lifecycle, and Java memory semantics are not the proposed C++ direct-slot
contract. The current revision pinned below no longer contains `WorkerPool` or
`WorkProcessor`; cite the historical revision when discussing them.

## Intentionally excluded

- the complete Java API, DSL, event-factory model, and handler lifecycle;
- direct translation of Java memory semantics into C++ memory orders;
- historical benchmark figures as current cross-platform evidence;
- an unqualified `zero-copy` or lock-free claim.

## Use in handoff

The repository already studies single-producer sequence claims, publication, reliable fan-out, a
fixed dependency chain, and per-slot multi-producer availability. The completed work-sharing
program adapted the shared-claim idea with its own C++ memory-model argument, bounded reuse proof,
and adversarial completion-hole tests. It does not implement the historical WorkerPool lifecycle.

## Primary sources

- [Official Disruptor user guide](https://lmax-exchange.github.io/disruptor/user-guide/)
- [Sequencer API at a fixed revision](https://github.com/LMAX-Exchange/disruptor/blob/c871ca49826a6be7ada6957f6fbafcfecf7b1f87/src/main/java/com/lmax/disruptor/Sequencer.java)
- [MultiProducerSequencer at the same revision](https://github.com/LMAX-Exchange/disruptor/blob/c871ca49826a6be7ada6957f6fbafcfecf7b1f87/src/main/java/com/lmax/disruptor/MultiProducerSequencer.java)
- [ProcessingSequenceBarrier at the same revision](https://github.com/LMAX-Exchange/disruptor/blob/c871ca49826a6be7ada6957f6fbafcfecf7b1f87/src/main/java/com/lmax/disruptor/ProcessingSequenceBarrier.java)
- [Original technical paper](https://lmax-exchange.github.io/disruptor/files/Disruptor-1.0.pdf)
- [Historical 3.4.4 WorkerPool at a fixed revision](https://github.com/LMAX-Exchange/disruptor/blob/a87bf422e42451b9e2d2d1f0f8de5b61ab561da2/src/main/java/com/lmax/disruptor/WorkerPool.java)
- [Historical 3.4.4 WorkProcessor at the same revision](https://github.com/LMAX-Exchange/disruptor/blob/a87bf422e42451b9e2d2d1f0f8de5b61ab561da2/src/main/java/com/lmax/disruptor/WorkProcessor.java)
