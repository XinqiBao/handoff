# Ordered-publication MPSC ring

## Question and comparison

`OrderedPublicationRing<T, Capacity, Sequence>` studies concurrent claims with one
contiguous producer publication frontier. Multiple producers and exactly one consumer
exchange fixed inline values without loss. Claim order is FIFO order. This is an
implementation comparison with a serialized route: a mutex held across a complete
producer operation on the basic SPSC ring. The latter permits only one producer
operation in flight; it does not isolate the cost of the publication frontier.

The structure is inspired by the classic DPDK multi-producer head/tail protocol,
not compatible with its API, ABI, algorithm, or memory model. It has no per-slot
readiness metadata, cancellation, helping, timeout, or producer-failure recovery.

## State and operations

Positions start at zero. `next_claim` is the first unclaimed position,
`published` is one past the last contiguous visible position, and `released` is
one past the last consumer-released position. All three start at zero and never
decrease or wrap. They satisfy `released <= published <= next_claim` and
`next_claim - released <= Capacity`. A position maps to `position % Capacity`;
physical slots wrap while logical positions do not. Every slot is a live,
default-constructed `T` until destruction. Capacity is exact: unfinished claims
and published but unreleased positions both occupy slots.

`try_claim()` acquires the consumer release cursor and uses CAS to grant one
exclusive position, or returns empty when full or the finite position domain is
exhausted. A successful producer claim grants mutable access to one slot. The
producer must finish writing it and call `publish()` exactly once. The claim
token is movable but cannot be cancelled; destroying or overwriting an active
token terminates the process. `try_publish()` checks whether `published` equals the claim's
position; it returns false without changing state across a hole, or advances the frontier and
returns true at its turn. `publish()` retries that check with a thread yield and returns only
after advancement.

The sole consumer calls `try_observe()` to acquire the next visible position,
reads its const slot, and calls `release()` after finishing all access. It may
hold only one observation at a time. Releasing makes that physical slot eligible
for a later claim. An observation may be dropped without release, in which case
the same position remains next to observe. An empty result changes no state.
The ring must outlive all tokens and participating threads.

The unsigned `Sequence` type defaults to `uint64_t`; a smaller type is useful
for boundary tests. The final claimable position is `max(Sequence) - 1`, so
`max(Sequence)` is a representable one-past cursor. After it is claimed,
`try_claim()` rejects all further claims even if storage is released. There is
no counter rollover or generation ambiguity. Capacity must fit in the domain.

## Progress and waiting

An earlier unfinished claim is a publication hole. Later producers may claim
distinct positions and finish their payload writes until all `Capacity` slots
are charged, including the hole. Their `publish()` calls wait for their turn and
cannot return across the hole. The consumer can read an already published
prefix, but cannot observe the hole or any later position. With a hole at the
first position, it cannot complete another handoff. Once the owner publishes,
waiting producers advance the frontier in order and the consumer can release
slots for reuse. If the owner never publishes, no later publication call can
return; eventually claims stop at full capacity. A producer blocked inside
`publish()` cannot itself make another claim, so the number of in-flight claims
also depends on the number of participating producers or outstanding tokens.

`try_claim()` does not wait for capacity, although CAS retries under contention
can starve and an older release observation may conservatively report full.
`try_observe()` returns immediately on empty. `publish()` busy-waits with a thread yield. The harness retries
failed claims and empty observations with a yield. A stalled producer can block
later producers and the consumer indefinitely. This route makes no lock-free or
wait-free progress claim. The serialized control instead blocks every later
producer operation at its mutex while the owner is delayed; its consumer can
still drain the prefix already inserted.

## C++ synchronization argument

CAS on `next_claim` gives unique position ownership. The producer reads
`released` with acquire ordering before permitting a claim whose physical slot
could be reused. The consumer's release store to `released` occurs only after
its final read of that slot, so an accepted reuse cannot race that read.
Earlier unreleased positions remain charged even if their payload is ready.

A producer writes its exclusively owned slot before publishing. Its acquire
load of `published` must observe the preceding producer's release store before
its own release store. This chains preceding payload writes through successive
publishers. The consumer acquire-loads `published` before reading a visible
slot, so it sees the payload for that slot and every earlier position in the
contiguous prefix. The consumer then release-stores `released` after reading;
a later claimant's acquire load closes the reuse path. TSan can exercise these
paths, but does not replace this happens-before argument.

## Evidence and limits

Mechanism tests deliberately pause an earlier claimant, complete a later
payload, verify the hole, close it, and check FIFO and reuse. They also cover
full in-flight capacity, physical wrap, finite counter exhaustion, unique
concurrent claims, and sustained payload integrity. The control and concurrent
route use the same fixed payload generator and consumer validation in benchmark
workloads. Mutex ownership, CAS retries, direct slot access, publication waits,
and cache traffic all differ, so throughput differences describe complete
implementations rather than one atomic operation.

The unfinished-claim obligation excludes exceptions or owner death after claim
unless the caller still completes publication. Recovery is a separate question.

## Measured observation

The controlled [two-producer implementation comparison](../experiments/015-mpsc-ordered-publication.md)
on one Intel N150 placement found all six ordered-tail rows above all six serialized-control
rows under a saturated 64-byte, 1024-slot workload. The serialized rows varied substantially, so
the median gap is not a precise stable cost estimate. The result compares whole routes and does
not isolate publication-tail coordination from producer serialization or payload access.
