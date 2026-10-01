# Ordered-release SPMC ring

`OrderedReleaseRing<T, Capacity, Sequence>` studies shared consumer claims with
one ordered release cursor. One producer publishes fixed inline values in
position order; the benchmark keeps one claim in flight per worker. The API
does not track worker identity and permits a caller to hold multiple claims.
Each slot contains a live default-constructed `T`. This is work sharing, not
fan-out.

The producer alone owns `next_publish` and one optional claim token. A
successful `try_claim()` checks `next_publish - released < Capacity`, grants
mutable direct slot access, and must be followed by `publish()` or `cancel()`.
`cancel()` leaves the unpublished position available to the producer. A release
store to `published` makes a completed value visible. Consumers CAS
`next_acquire` to acquire one unique published position, then read its const
slot until final access and `release()`. There is no cancellation after
consumer acquisition. Destroying an active token terminates. The ring must
outlive every token and participating thread.

`try_release()` returns false across an earlier release hole while retaining
the token; `release()` busy-spins until that attempt succeeds. At its turn, the
owner release-stores `position + 1` to `released`. Later owners can process
concurrently but cannot return from `release()` before predecessors. A stalled
or abandoned owner prevents their calls from returning and eventually fills
capacity. Acquisition is FIFO in position, not in wall-clock processing
completion or worker fairness. User effects can fail. No recovery, lease,
timeout, or operation-wide lock-free/wait-free guarantee is provided; CAS
retry can starve.

The producer publishes the contiguous interval `[0, published)` with a release
store. Every consumer attempt requires `next_acquire < published` from its own
acquire load before CAS; that load orders all producer writes through the
observed prefix before slot access. A relaxed claim-cursor observation can be
newer than an independently stale publication observation, so equality alone
is insufficient: `next_acquire >= published` rejects conservatively. A failed
CAS refreshes the candidate cursor and the loop rechecks publication before
retrying. A successful relaxed CAS grants one unique position but provides no
payload synchronization itself. Finite, nonwrapping positions make ordinary
ordering valid and keep the terminal one-past cursor unclaimable.

The final slot read precedes the owner's acquire load of
`released` and release store of its successor. When later owners observe that
store with acquire, the sequence of releases carries all earlier owners' slot
reads to the final release cursor. The producer acquire-loads `released`
before reusing any slot. A stale acquire observation may reject a producer
claim conservatively. Adjacent atomic cursors and slot payloads can share
cache lines; the implementation isolates the semantic question rather than
padding a measured layout. Logical positions never wrap: the last claim is
`max(Sequence) - 1`, with `max(Sequence)` a terminal one-past cursor.

Latch tests hold the first owner while two later owners acquire, check both
failed nonblocking releases and full-capacity rejection, then close the hole.
Another test holds a newer owner while the earlier prefix becomes reusable.
Shared tests cover repeated physical wrap, unique delivery, payload integrity,
the single-slot empty/publication boundary, and finite exhaustion. A tiny
observation model enumerates independent stale publication/claim observations,
including failed-CAS refresh, and exposes the former equality-only acceptance.
It does not simulate the full C++ memory model or force stale reads in a live run.

The controlled [consumer-coordination comparison](../experiments/017-spmc-consumer-coordination.md)
retains this route because later workers can acquire and process across a hole
even though release calls wait. Its N150 rate lies between serialization and
per-slot completion in the canonical rows, with one unexplained low row. Those
measurements predate the publication-bound repair and describe their recorded
revision; they do not measure the corrected acquisition loop.
