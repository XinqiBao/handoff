# Experiment: Consumer coordination and work sharing

- Type: complete-route implementation comparison with untimed semantic tests
- Status: correctness implementation complete; controlled measurement pending
- Measurement revision: pending clean CI-green revision

## Question and routes

With one ordered producer and two competing consumers, how do unique
acquisition, processing completion, release-call return, producer-visible
reclamation, and physical reuse differ when owners finish out of order?
The serialized control holds a consumer mutex through acquisition, processing,
and release. The ordered-release ring shares a CAS claim cursor but waits for
each predecessor at release. The slot-completion ring shares the claim cursor
and marks exact-generation completion independently; the sole producer scans
the contiguous reusable prefix. Their exact contracts and C++ ordering proofs
are in the corresponding mechanism notes.

The fixed two-consumer complete-handoff throughput command uses exact 64 or
1024-slot capacity, 8, 64, or 256-byte inline payloads, one producer, and two
workers. All routes generate and validate the same position-derived bytes,
retry with yield, track every position once with a per-position atomic seen
count, and record each worker's acquisition count. Warmup and timed phases
have the same synchronization boundaries. The producer ends timing only after
both consumers release all work and it verifies the final reusable prefix.
The numerator is fully processed, producer-verified publications. Fan-out
delivers each position twice and is not an equivalent rate control.

## Deterministic facts

The latch tests publish three positions and hold the first owner. Serialization
prevents later consumer acquisitions while its mutex is held. Ordered release
permits later acquisitions and payload work, but later nonblocking release
attempts fail and their release calls wait. Per-slot completion permits both
later releases to return, while the producer still rejects further claims at
full capacity. When the first hole closes, the producer discovers the slot
route's completed prefix even with a newer owner unfinished. Another test
shows ordered release exposes only the prefix whose release callers have
actually advanced the shared cursor. Shared tests check unique delivery and
payload integrity over repeated physical wrap; route-specific tests repeat
holes near terminal `uint8_t` positions. No test infers worker fairness from
the observed distribution.

## Controlled run plan

Use a clean detached execution clone at the exact CI-green SHA. On the
historical four-core N150, pin the coordinator to CPU 0, producer to CPU 1,
and two consumers to CPUs 2 and 3. Retain the three-owner hole as an untimed
diagnostic. Check current topology, stock policy, interference, temperature,
and throttle counters before and after the group. Build and test native
Release in the clone, then condition the host and run balanced paired blocks
of the three routes at one canonical 64-byte, 1024-slot workload. Preserve raw
CSV and host sidecars under ignored `results/`. A rate comparison describes
complete routes and cannot isolate mutex, CAS, tag, or coherence cost.

## Limits

All mechanisms require cooperating owners to call release. An abandoned
owner can permanently block capacity. Exactly-once successful user effects,
fairness, recovery, leases, timeouts, cancellation after acquisition,
operation-wide lock-freedom, and MPMC composition are not established.
