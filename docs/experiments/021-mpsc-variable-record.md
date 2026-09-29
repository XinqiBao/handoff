# Experiment 021: Paired MPSC descriptor and byte credit

- Type: semantic storage/lifetime study
- Status: local validation complete; CI pending
- Mechanism revision: pending implementation commit and CI provenance
- Date: 2026-09-29 UTC

## Question

Can two producers atomically assign descriptor order and variable byte extents, then write and
complete independently, while one FIFO consumer returns both credits only after its final read?
The [mechanism note](../mechanisms/mpsc-variable-record.md) owns the role contract and C++
happens-before argument. The SPSC descriptor/payload ring has variable byte credit but no competing
reservations or completion holes. The fixed-slot MPSC ring has those holes but no variable byte
extent or wrap gap. Their existing tests do not establish the paired admission and release invariant.

## Semantic evidence

The deterministic tests charge zero-byte records to descriptors alone, fill bytes while descriptors
remain available, and hold a consumer observation to show that reading alone returns neither
credit. Another test advances to a physical suffix shorter than the next footprint, then holds that
wrapped claim while a later producer publishes. Observation stops at the held descriptor. Closing
the hole exposes the ready successor; release of the wrapped record returns its complete gap and
payload extent even while a newer claim remains unfinished. The next claim uses exactly the newly
freed byte credit. An 8-bit ordinal test consumes all 255 claimable positions and rejects rollover.

The concurrent test runs two producers and one consumer through 40,000 mixed-length records,
including zero-length records and repeated physical wrap. The consumer checks ordinal order,
header identity, length membership, and payload bytes. This checks exercised cooperative histories,
not every interleaving or abandoned-owner behavior.

## Interpretation and limits

The new invariant is paired reservation and paired release of two differently measured bounded
resources. The reservation mutex intentionally isolates this lifetime question from a CAS protocol
for two cursor dimensions. No rate was measured: the mutex, direct payload access, capacities, and
operation shape differ from earlier mechanisms. The result does not establish fair producer service,
failure recovery, lock-free progress, or an ordering rule for payload work outside descriptor FIFO.

Local Clang/C++23 Debug, Release, ASan/UBSan, and TSan builds passed, with all 168 tests passing
in each preset. The format check and clang-tidy build passed. CI provenance remains pending.
