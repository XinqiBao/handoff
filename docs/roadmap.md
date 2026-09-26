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

The next research boundary is concurrent ownership of bounded storage. With multiple producers,
claim order can differ from completion order. A design must separately account for exclusive
reservation, payload readiness, externally visible ordered progress, and safe reuse. A delayed
producer can hold a publication hole even while later producers finish. That semantic distinction,
rather than a topology checklist, motivates the first Phase II package.

## First research package: producer claims and ordered visibility

Question: when two producers reserve adjacent positions and complete payload writes in reverse
order, what progress can each make while a single consumer must observe FIFO order and storage
remains bounded?

The first comparison should keep fixed-size inline slots, one consumer, lossless delivery, scalar
operations, and the same payload work. A serialized producer route is a control for the cost and
progress limits of exclusive whole-operation ownership. A concurrent-claim route should reserve
distinct positions and publish one contiguous tail in claim order, exposing the classic ordered
publication dependency. These are structural inspirations from DPDK, not API ports. Implement the
smallest control that makes the comparison interpretable; an existing SPSC ring guarded by a
producer lock may suffice if the exact ownership boundary is stated.

Before implementation, write the precise API and invariants: claim uniqueness; capacity charged
at reservation, including unfinished claims; no consumer access past a hole; release/acquire paths
from each payload write to consumer read and from consumer release to producer reuse; generation or
counter handling at physical wrap; finite sequence behavior; and the fate of an unfinished claim.
In particular, an ordered tail cannot advance past an abandoned claim without an explicit recovery
protocol. Do not silently promise cancellation or nonblocking completion.

Use controlled scheduling in tests to pause the first claimant after reservation, finish the next
payload, check that the consumer sees no hole, then release the first and verify order and safe
reuse across wrap. Also test full capacity with claims in flight, unique ownership, and sustained
integrity. TSan supports exercised executions; the C++ memory-order argument belongs in the
mechanism note.

Measure both a steady-state control comparison and a bounded producer-stall scenario if the latter
can isolate reservation, completion, visible publication, consumer completion, and in-flight work.
Define accounting and phase boundaries before collecting performance data. A test or smoke run
alone is not controlled evidence. The stall result should determine whether per-slot availability
or relaxed/cooperative tail advancement is the next useful comparison.

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
