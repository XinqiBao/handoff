# Serialized-consumer SPMC control

`SerializedConsumerRing<T, Capacity, Sequence>` has one producer and competing
consumers, but permits one consumer operation at a time. It is a control for
whole-operation serialization, not a transfer of an SPSC consumer role. Each
fixed inline slot contains a live default-constructed `T`.

The producer alone owns `next_publish` and may hold one claim token. A successful
`try_claim()` reserves the next position when `next_publish - released < Capacity`.
It grants mutable access until `publish()`, which release-stores the one-past
position. `cancel()` abandons an unpublished producer claim without advancing
the cursor. The producer must not access the slot after publishing.

Each `try_acquire()` takes the consumer mutex. On empty it returns no token and
unlocks. On success the token retains the mutex across all processing and
`release()`. The token gives const direct slot access. `release()` advances both
the mutex-protected acquisition cursor and atomic reusable prefix, then unlocks.
The mutex itself enforces one outstanding consumer claim across all workers.
Destroying an active consumer token terminates: cancellation after acquisition
would otherwise create a permanent ownership hole. The ring must outlive tokens
and participating threads. A consumer blocked on the mutex cannot acquire a
later position while an earlier owner is stalled. The producer can publish
until exact capacity fills. No owner fairness or successful user effect is
promised. A stalled owner can permanently prevent reuse.

Producer publication is a release store and consumer acquisition reads it with
acquire, making the payload write visible. The mutex grants unique consumer
ownership. The final consumer slot access precedes its release store to
`released`; the producer acquire-loads that value before reusing a slot. The
cursor is monotonic and physical index is `position % Capacity`. The last
claimable position is `max(Sequence) - 1`; the one-past maximum is terminal.
There is no logical rollover. A stale release observation can reject a claim
conservatively. The mutex can block indefinitely, so this route has no
operation-wide lock-free or wait-free guarantee.

The latch test holds an owner with a full ring, checks that a second consumer
cannot return and the producer cannot overwrite, then closes the hole and
checks physical reuse. A `uint8_t` test checks the finite boundary. In the
controlled [consumer-coordination comparison](../experiments/017-spmc-consumer-coordination.md),
this control measured below both concurrent routes on the N150 canonical
workload; it differs in mutex admission and permitted processing overlap, so
the gap is a complete-route observation.
