# Per-slot completion SPMC ring

`SlotCompletionRing<T, Capacity, Sequence>` studies independent consumer
release with producer-discovered contiguous reuse. It has one ordered producer,
competing consumers, live fixed inline slots, and one owner per published
position. Consumers have const direct slot access until their final read and
`release()`. The ring must outlive its tokens and threads.

The sole producer claims at most one position at a time and writes its slot.
`publish()` release-stores the next one-past position; `cancel()` leaves an
unpublished claim available. Consumers acquire-load `published` and CAS
`next_acquire`, granting one unique position each. A consumer `release()`
release-stores the exact `position + 1` tag in its physical slot and returns
without waiting for another owner. After the last access it must not touch
the slot. Active token destruction terminates, and acquisition has no
cancellation or recovery protocol.

Only the producer scans completion tags from its oldest unreusable position.
Each matching acquire load advances its private `reusable` cursor; a mismatch
ends the scan. `try_claim()` scans before its exact-capacity check. A stalled
early owner leaves a hole, so later completed calls can return while the
producer eventually fills the ring. After that owner releases, the producer
can discover all already-completed successors without another consumer action,
even when a newer owner is still unfinished. The first still-owned physical
slot blocks cyclic wrap. The generation tag is the one-past logical position;
a prior generation has a different value. The final claimable position is
`max(Sequence) - 1`, leaving the terminal one-past value representable.
Logical positions never wrap, so tag equality cannot be confused by rollover.

The publication release/acquire pair orders the producer write before the
owner's read. Claim CAS grants exclusive ownership. The owner's last read
precedes its per-slot release store; the producer's acquire load of the
matching tag orders the next write of that physical slot after the owner's
access. Repeated scans may see a stale mismatching tag and reject a claim
conservatively. Each slot tag is atomic; adjacent tags may share a cache line
and cause coherence traffic, as may the shared claim cursor. CAS retry can
starve, and a stalled owner can block bounded reuse indefinitely. No
operation-wide lock-free/wait-free, fairness, successful user effect, or
participant-failure recovery guarantee follows from independent release.

Latch tests hold the first owner while two later owners return from release,
then check that the producer alone discovers the completed prefix when the
hole closes. A second test leaves a newer owner unfinished. Shared tests
cover exact capacity, repeated physical wrap, unique delivery, payload
integrity, and finite exhaustion.
