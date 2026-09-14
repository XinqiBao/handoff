# Bounded sequence pipeline

## Purpose and isolated difference

`BoundedSequencePipeline<T, Capacity>` adds a fixed dependency chain to sequence-addressed
publication. One producer publishes each sequence, an upstream consumer observes and releases it,
and only then may a downstream consumer observe and release it. Producer reuse is gated by the
downstream stage. The Q1 single-consumer ring and Q2 independent fan-out remain unchanged.

The mechanism isolates Disruptor-inspired consumer dependency gating. It is not an LMAX API or Java
port and does not add a general sequence barrier, arbitrary dependency graph, dynamic registration,
multiple producers, work sharing, per-slot availability, batch claims, or waiting policies.

## Representation and finite sequence domain

The ring owns `Capacity` default-constructed inline `T` slots. Sequence `n` maps to physical slot
`(n - 1) % Capacity`, every slot is usable, and sequences start at one. `PipelineStage::upstream`
and `PipelineStage::downstream` are permanent compile-time roles with distinct observation token
types.

The producer owns non-atomic next-to-claim and active/exhausted state plus an atomic cursor for the
highest contiguous publication. Each stage owns non-atomic next-to-observe and active/exhausted
state plus an atomic gating sequence for the highest sequence it has released. All cursors begin at
zero. The producer and each stage are confined to their declared owner thread.

The default `std::uint64_t` sequence domain ends at `UINT64_MAX` and never wraps. Publishing the
limit exhausts the producer; each stage becomes exhausted after releasing that limit. An optional
unsigned sequence type supports smaller-domain tests, and capacity must fit in that type.

Slots remain alive until ring destruction and are reused through caller assignment. Observations
return `const T&`; releasing a slot neither moves from nor destroys it. Resources remain retained
until a later producer assignment overwrites the slot. `T` must be default-initializable.

## Claim and observation contract

`try_claim()` returns one move-only token only when no producer claim is outstanding, the finite
sequence range remains available, and the next sequence is within exact capacity of the downstream
gating sequence. The token exposes its sequence and mutable slot. `publish()` makes the populated
slot visible. `cancel()` or token destruction publishes nothing and lets the producer retry the
same sequence.

`try_observe_upstream()` requires the producer cursor to reach the upstream stage's next sequence.
`try_observe_downstream()` instead requires the upstream gating sequence to reach the downstream
stage's next sequence. Each returns its own move-only token exposing the sequence and const slot.
`release()` advances that role's gating sequence. `cancel()` or destruction leaves its progress
unchanged, so the same role retries the same sequence. A role cannot hold nested observations.

Move construction transfers responsibility and deactivates the source. Move assignment first
cancels a token already held by the destination. Publishing, releasing, or cancelling an inactive
token is harmless; accessing its value is outside the contract. Tokens do not cross owner threads,
and the pipeline outlives every token.

## Capacity and memory ordering

Producer occupancy is `(next_to_claim - 1) - downstream_gating`. A claim is refused when occupancy
is at least `Capacity`, so upstream progress alone cannot make storage reusable. A stale producer
acquire-load of downstream progress only rejects reuse conservatively.

Producer slot writes are sequenced before its release-store to the publication cursor. The upstream
stage acquire-loads that cursor before reading the slot, so publication makes those writes visible.
Its reads are sequenced before a release-store to the upstream gating sequence. The downstream
stage acquire-loads that sequence before reading the same slot, extending the happens-before chain
from producer through upstream to downstream.

Downstream reads are sequenced before its release-store to the downstream gating sequence. The
producer acquire-loads that sequence before reusing capacity, so both stages' reads happen before a
subsequent overwrite. Each cursor stays contiguous because every owner advances one sequence at a
time and holds at most one active token. No per-slot availability state is required.

Stage state is stored directly without cache-line padding. This keeps Q3 focused on dependency
ordering; state placement would be a separate experiment.

## Correctness and benchmark coverage

Tests cover initial state, fixed roles, dependency ordering, unavailable downstream observations,
claim and per-stage cancellation, nested rejection, exact capacity, downstream-gated wrap reuse,
move-only tokens, finite-limit rejection, resource retention, and a long concurrent run with
independently paced stages.

The `pipeline` throughput workload instantiates one producer and two consumer-stage threads. Both
stages perform equivalent payload validation and independent checksum work. Timing stops only after
the downstream stage releases every timed publication; `iterations` and `messages_per_second`
count those end-to-end completions. Controlled runs accept a producer CPU and an ordered pair
mapping to upstream and downstream, with verified per-role result metadata. Unpinned smoke timings
remain plumbing evidence only.

## Provenance

Dependency gating comes from the repository's
[LMAX Disruptor inspiration note](../inspirations/lmax-disruptor.md). This implementation supplies
its own C++ ownership and acquire/release argument and makes no compatibility claim.
