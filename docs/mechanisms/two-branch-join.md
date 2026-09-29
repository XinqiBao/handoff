# Fixed two-branch write/join

`TwoBranchJoin<Input, Left, Right, Capacity, Sequence>` has one ordered producer, one ordered owner
of each branch, and one ordered join consumer. It uses exactly `Capacity` live inline slots. Each
slot contains three separately owned subobjects: producer-written input, left-written result, and
right-written result. Branch tokens expose a const input and only their own mutable result. The
join token exposes all three as const references. The ring and all slot objects must outlive every
token and participating thread. This is a fixed topology, not a graph or general queue API.

The producer claims its next position, writes input, and release-publishes the one-past position.
Each branch acquire-loads that cursor before taking its next position. A branch may finish
independently of the other and release-stores its one-past completion cursor. The join consumer
acquire-loads both completion cursors and can observe only its next position after both have passed
it. A fast branch can finish later positions while the other holds an earlier one, but the join
cannot bypass that hole. Branches are individually ordered and have one active token each; there
are no within-branch out-of-order completion holes. The join releases only after its final access.
Cancellation of an active token retries the same position and does not publish, complete, or release
it. No token may be used from a different owner thread.

Producer input writes happen-before each branch's input read through the producer's release
publication and that branch's acquire. Each branch's result writes happen-before the join's reads
through its own release completion and the join's matching acquire. The join takes both acquires
before touching the slot, so it has both independent write paths, not merely one linear predecessor
chain. The branches read the common input concurrently and write disjoint result subobjects; they
do not read each other's result. The join's final reads precede its release store to `released`.
The producer acquire-loads `released` before reusing that physical slot, so its next write is after
the final join access. Join release authorizes reuse; the later producer assignment actually reuses
storage. The branches have already completed before join acquisition, so no prior-generation branch
access can overlap that write. A cancelled join observation retains the slot and blocks reuse.
The producer changes only input on a new generation. Each branch must overwrite its retained result
before completing that generation, and the join requires both fresh completion cursors; old result
contents alone never grant visibility. A stale cursor load may delay an operation but cannot grant
an uncompleted position.

Capacity is exact: claims, published positions, independently completed positions, and held join
observations all occupy a slot until final join release. A slow branch eventually backpressures the
producer at wrap even when the other branch is complete. A slow join consumer does the same. The
ordered cursors give FIFO at producer, branch, and join positions. This contract assumes cooperative
completion; an abandoned branch or join owner creates a permanent bounded-progress hole. It does
not recover owners, schedule branches fairly, or guarantee operation-wide lock-free progress.

Positions start at zero and do not wrap. The last claimable position is `max(Sequence) - 1`, whose
one-past cursor is representable; later claims fail even after every slot is released. Physical
slots wrap modulo `Capacity`. Tests cover cancellation, an externally held branch across the other
branch's later completion, exact capacity and held join release, physical wrap, all 255 claimable
`uint8_t` positions, and a four-role 30,000-position concurrent integrity run. Unlike read-only
fan-out, both branches contribute independent writes. Unlike the single-owner pipeline, the join
requires both completion happens-before paths. Although eligibility checks two ordered cursors,
the new obligation is the combined visibility and final lifetime of their disjoint results.
