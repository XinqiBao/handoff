# Research Direction

This document records the current research boundary and criteria for promoting a question into
work. Mechanism notes describe implemented contracts; experiment records own measured evidence;
Git preserves the completed stage history. Candidate directions below are not an execution queue.

## Current state

The initial single-producer phase is complete. The repository has bounded SPSC counter, layout,
batch, bulk/burst, and staged variants; sequence publication, reliable fan-out, and a fixed
dependency pipeline; three record-storage layouts; and lossy sequence-addressed metadata and
payload observation. Controlled Linux records cover selected scalar and grouped SPSC comparisons,
sequence contracts, lossy offered load, and a selective PMU follow-up. They establish conditional
results for one host and workload set, not a general ranking. The mechanism and experiment indexes
identify the implemented variants and completed evidence.

The first Phase II package implements concurrent ownership of bounded storage. With multiple
producers, claim order can differ from completion order. The ordered-tail mechanism distinguishes
exclusive reservation, payload readiness, externally visible ordered progress, and safe reuse.
Its deterministic hole diagnostic shows that a delayed first claimant allows later producers to
claim and finish payload work up to capacity, while their publication calls and the consumer
remain behind the hole. This is a semantic and progress result, not a performance ranking.

## First research package: producer claims and ordered visibility

Question: when two producers reserve adjacent positions and complete payload writes in reverse
order, what progress can each make while a single consumer must observe FIFO order and storage
remains bounded?

The implemented comparison keeps fixed-size inline slots, one consumer, lossless delivery, scalar
operations, and the same payload work. The serialized route holds a mutex across payload
generation, full retries, and the basic SPSC push. The concurrent route reserves distinct
positions with CAS and publishes one contiguous tail in claim order. These are structural
inspirations from DPDK, not API ports. The [mechanism note](mechanisms/ordered-publication-mpsc.md)
owns the exact contract and memory-model argument.

Claims charge capacity immediately. No consumer access crosses a hole; producer-to-consumer and
consumer-to-reuse paths use release/acquire synchronization. Logical positions never wrap, though
physical slots do. A claimed producer must eventually publish: cancellation and abandoned-claim
recovery are outside this package.

Controlled tests pause the first claimant, finish later payloads, verify that the consumer sees
no hole, then close it and check FIFO and reuse across wrap. They also cover full capacity,
concurrent claim uniqueness, finite exhaustion, and sustained integrity. TSan supports exercised
executions; the C++ memory-order argument remains in the mechanism note.

The steady-state comparison measures completed handoffs. A separate bounded publication-hole
diagnostic records reservation, payload completion, publication return, visibility, consumer
completion, and backpressure at a controlled phase boundary. It does not measure rates or stall
duration. Controlled Linux performance evidence remains a separate gate; smoke runs do not
support a ranking. The diagnostic establishes that later completion is useful bounded in-flight
work but cannot advance visibility under the chosen FIFO frontier.

This package excludes multi-consumer ownership, variable-size allocation, cancellation/recovery,
general wait policies, DPDK compatibility, and generic benchmark dispatch infrastructure.

## Candidate questions

- **Publication progress:** Can per-slot generation-tagged readiness or cooperative frontier
  advancement let later finishers return without making later messages visible across a hole?
  Compare only after the ordered-tail baseline exposes a meaningful stall or coordination cost.
- **Competing consumers:** How should each publication acquire exactly one consumer owner, unlike
  existing reliable fan-out? Study consumer release and reuse separately from producer contention.
- **Dependencies:** What changes when a fixed dependency chain becomes a small static fork/join,
  such as `P -> {A, B} -> C`? Avoid a runtime graph framework.
- **Ordered stages:** SORING-like acquire/release and helping combine worker ownership with a
  contiguous stage frontier. Consider this after the simpler ownership and progress obligations are
  understood.
- **Storage pressure:** Revisit record layouts only for a stronger workload, such as mixed lengths,
  wrap padding, independent descriptor/byte limits, or payload lifetime. Existing fixed-length
  benchmark modes do not settle those questions.

An SPSC path per producer with consumer-side polling is another useful architectural control if a
shared producer claim structure proves costly. It changes polling, fairness, and global ordering,
so it must be evaluated as an implementation comparison with an explicit merge policy.

## Promotion criteria

Promote a direction only when it has a precise semantic question, a small isolatable mechanism or
comparison, explicit invariants and C++ memory-order reasoning, deterministic adversarial and
concurrent tests, a workload that distinguishes the alternatives, and interpretable observations
relative to mechanisms already present. Preserve useful intermediate variants. Validate and review
each coherent research package, record only completed work as completed, commit, push, and check CI.
Do not create speculative experiment protocols or infer performance from benchmark smoke runs.
