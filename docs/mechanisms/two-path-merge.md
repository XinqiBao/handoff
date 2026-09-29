# Two producer-owned paths and one merge consumer

`TwoPathMerge<T, PathCapacity>` fixes two independent staged SPSC paths and one consumer. Each
producer owns only its path. The consumer selects an eligible path head, reads its live inline slot
directly, then releases that path. This is a topology study, not a new SPSC publication primitive.
The path implementation is `StagedBoundedRing`; no global claim cursor or ordering sequencer exists.

Each path is FIFO. The consumer's successful acquisitions define the merge order, with no promised
order between producers, publication calls, or wall-clock times. Polling starts with the path other
than the last selected path, then tries the other path if its preferred head is unavailable or held.
The initial preference is first. A successful selection changes preference to the other path;
repeated selection of the sole eligible path leaves preference unchanged. If both heads remain
eligible at every acquisition, selections alternate. This is a conditional selection rule, not a
scheduling or wall-clock fairness guarantee. `try_acquire()` is nonblocking and never waits for an
unpublished or idle producer. One consumer thread owns preference and all observation tokens; it
may hold one token per path at once. Producer reservation tokens remain on their owner threads.

Each path has exactly `PathCapacity` usable slots, for `2 * PathCapacity` physical slots in total.
An unpublished reservation and an unreleased observation occupy their own path. A full path rejects
its producer even if the other path is empty; spare slots cannot be borrowed. A stalled first-path
producer or consumer does not prevent second-path publication and release. An abandoned participant
can permanently stop its path, and no fairness, recovery, or operation-wide lock-free guarantee is
claimed. The test harness yields on retries.

For each path independently, producer writes precede the release store of its tail. Consumer acquire
of that tail precedes its slot read. The consumer's final access precedes its release store of that
path's head; the producer acquire-loads that head before a later reservation can reuse the physical
slot. `release()` authorizes reuse; the next producer write actually reuses it. `cancel()` ends an
observation without advancing the head, so the same head is retried and reuse remains forbidden.
The consumer's merge preference is private state and needs no atomic ordering. These C++
happens-before paths never cross between slots on different producer paths. Slots are live,
default-constructed `T` objects throughout the topology's lifetime; construction and assignment
may manage resources, and the topology must outlive all tokens and threads.

The underlying SPSC counters use unsigned bounded-distance arithmetic and may wrap; capacity is at
most half the counter range. There is no finite nonwrapping sequence limit of the kind used by the
shared MPSC rings. Deterministic tests cover selection, an unpublished path, held and cancelled
observations, exact per-path capacity, and independent physical wrap. A two-producer integrity test
checks both FIFO streams through repeated wrap. The shared MPSC routes instead assign global FIFO
positions using a common claim authority. Their earliest publication hole can stop observation
across both producers, whereas a hole in one path here is local. The capacity and ordering contracts
differ, so any future rate comparison must state those differences rather than present an isolated
coordination cost.
