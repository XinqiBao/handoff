# Bounded sequence ring

## Purpose and isolated difference

`BoundedSequenceRing<T, Capacity>` is the smallest one-producer/one-consumer mechanism in this
repository that gives each slot handoff a monotonic sequence. It separates producer claim, slot
population, publication cursor advancement, consumer observation, and consumer gating progress.

The structure is inspired by LMAX Disruptor sequencing, but it is not an LMAX API or Java port. It
does not provide event factories, barriers, handler lifecycles, multiple producers, multiple
consumers, dependency graphs, or per-slot availability tracking.

## Representation and finite sequence domain

The ring owns `Capacity` default-constructed inline `T` slots. Every slot is usable, and sequence
`n` maps to physical slot `(n - 1) % Capacity`. Sequences start at one and never wrap. The default
`std::uint64_t` sequence type therefore admits publication through `UINT64_MAX`; after that sequence
is published or released, the respective side rejects further progress. The optional unsigned
sequence template parameter makes this finite-domain behavior testable with a smaller type; it does
not change the one-based rule.

Producer-owned `next_to_claim_` is distinct from atomic `producer_cursor_`, which records the
highest contiguous published sequence. Consumer-owned `next_to_observe_` is distinct from atomic
`consumer_gating_sequence_`, which records the highest fully released sequence. Both atomic cursors
start at zero. Public cursor accessors are observations for tests and diagnostics; they do not claim,
publish, observe, or release anything.

All slots remain alive until ring destruction and are reused through caller assignment. A consumer
receives only `const T&`, so release does not move from or destroy the slot. Resources remain in a
released slot until a later producer assignment replaces them. `T` must be default-initializable;
the caller's population operation supplies any further mutation or assignment requirement.

## Claim and observation contract

`try_claim()` returns a move-only producer token only when the finite sequence domain has not been
exhausted, no producer claim is outstanding, and fewer than `Capacity` published-but-unreleased
sequences occupy the ring. The token exposes its sequence and mutable slot value. `publish()` makes
that exact sequence visible and advances producer-owned next-to-claim state. `cancel()` and token
destruction make no sequence visible, so the next claim retries the same sequence.

`try_observe()` returns a move-only consumer token only when the producer cursor has reached the
next sequence and no observation is outstanding. The token exposes its sequence and a const slot
value. `release()` advances the gating sequence, permitting eventual slot reuse. `cancel()` and
destruction leave gating unchanged, so the same sequence is observed again.

Move construction transfers responsibility and deactivates the source. Move assignment first
cancels any token held by the destination. `publish()`, `release()`, and `cancel()` are harmless on
an inactive token; `value()` requires an active token. Only the producer thread may claim and use
producer tokens, and only the consumer thread may observe and use consumer tokens. The ring must
outlive every token referring to it. Nested operations and cross-thread token transfer are outside
the supported contract.

## Capacity and memory ordering

The producer computes occupancy as `(next_to_claim - 1) - gating`. It refuses a claim when that
distance equals exact capacity, so no physical slot can be overwritten before consumer release.
The consumer refuses observation until the producer cursor reaches its exact next sequence. With
one outstanding operation per side, publication and release are contiguous by construction and do
not require per-slot availability state.

Producer claim acquire-loads the consumer gating sequence before allowing reuse. Producer writes
through the claim are sequenced before `publish()` release-stores the producer cursor. A consumer
acquire-load that observes that cursor synchronizes with the publication and therefore sees the
slot writes. Consumer reads are sequenced before `release()` release-stores the gating sequence; a
producer acquire-load must observe that release before it may reuse the corresponding slot. The
side-local next values, active-token flags, and exhausted flags are non-atomic because each belongs
to exactly one thread under the SP/SC contract.

## Correctness and benchmark coverage

Tests cover initial cursor values, empty and exact-full behavior, one-based sequences, publication
visibility, release gating, physical wrap, nested rejection, explicit and destructor cancellation,
move-only tokens and move replacement, resource retention, finite-limit rejection, and a long
concurrent sequence/payload integrity run.

The scalar `sequence` throughput mode generates each payload into the claimed slot, observes it
through the consumer token, and counts completion only after release. Ping-pong uses two identical
rings and the same local request/response payload flow, validation, waiting, timing boundaries, and
CSV meaning as the other scalar modes. The sequence implementation's explicit tokens, direct slot
access in throughput, const observation, and extra side-local state are real structural differences;
the benchmark must not attribute any future result solely to sequence numbering.

## Provenance

Claim/populate/publish, producer cursor, and consumer gating concepts come from the repository's
[LMAX Disruptor inspiration note](../inspirations/lmax-disruptor.md). This baseline applies a direct
C++ acquire/release argument and deliberately excludes the surrounding Disruptor API and runtime.

## Measured observation

On the controlled Intel N150 configuration recorded in
[experiment 008](../experiments/008-sequence-publication-comparison.md), saturated throughput did
not support a ranking against the basic head/tail ring: the sequence median was 2.359% lower, but
dispersion was larger and paired block direction reversed. Sequence ping-pong RTT was 3.173% higher
at the pooled median and higher in all three paired blocks, but the effect was close to dispersion
and sensitive to command position. This is a small conditional direction for the complete
implementation, not a precise stable penalty or an isolated cost of sequences, tokens, direct slot
access, or one atomic operation.
