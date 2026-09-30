# Slot-availability MPSC ring

`SlotAvailabilityRing<T, Capacity, Sequence>` asks whether the consumer can find
the contiguous ready prefix after an early hole closes without another producer
acting. It adapts per-slot generation-tagged availability from the Disruptor's
multi-producer sequencer. It is not compatible with that API or Java memory model.

CAS on `next_claim` grants a unique FIFO position and mutable physical slot.
Claims are admitted only while `next_claim - released < Capacity`, so unfinished
claims and unreleased observations both charge exact capacity. After payload
completion, `publish()` release-stores `position + 1` into that slot's atomic
availability tag and returns. The consumer acquire-checks the tag for its next
required position; a match grants a const observation. It stops at the first
unready position even if later tags are ready. Its release cursor moves only after
the final read and gates physical reuse. A cancelled observation is retried.

The ready tag is a full logical position plus one, not a boolean. On reuse, the
old generation cannot match the new position. The producer need not reset it:
capacity gating prevents the next owner from writing until the consumer releases
the old generation. All slots remain live, default-constructed `T` objects; the
ring must outlive every token and thread. A successful claim must publish once;
destroying an active claim terminates. There is no cancellation or owner-death
recovery. A stalled owner blocks FIFO observation at its position, but later
publication calls can return until capacity fills. Closing the hole makes the
already-ready prefix discoverable by the consumer alone. Claim CAS may starve;
an acquire release-cursor read may conservatively report full. No wait-free or
operation-wide lock-free guarantee is made; harness retries yield.

The producer's payload writes precede its release tag store, which the consumer's
acquire tag load observes before reading the slot. The consumer's final read
precedes its release store to `released`; a successful reusing claimant acquires
that cursor before writing. Thus neither concurrent payload access nor tag
overwrite can occur across generations. The claim CAS is relaxed and supplies
unique ownership through its modification order. Each tag has one writer per
generation, while the consumer reads only the next required generation.

Positions do not roll over. The last claimable position is `max(Sequence) - 1`,
with representable `position + 1`; later claims fail even if slots are free.
This also prevents a wrapped tag from matching an old generation. Shared claim
and release atomics can contend; per-slot tags avoid one producer publication
tail but adjacent tags may share cache lines, and payload slots may also share
lines. The unpadded layout is an explicit complete-route comparison, not a claim
that false sharing is absent.

Deterministic tests cover the hole, consumer prefix discovery with a newer claim
unfinished, full capacity, wrap and release, finite exhaustion, and concurrent
integrity. The common publication-hole diagnostic and scalar MPSC throughput
workload exercise the route. Sanitizers cover executions, not all interleavings.

The controlled [producer-completion comparison](../experiments/016-mpsc-producer-completion.md)
records the original complete-route N150 observation. A later
[fixed-frequency count/slot comparison](../experiments/022-fixed-frequency-mpsc-comparison.md)
rechecks that pair under workload-qualified host controls.
