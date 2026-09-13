# Sequence-Addressed Metadata Ring

## Purpose

`SequenceMetadataRing<Capacity, Sequence>` studies a compact, single-producer metadata ring with
direct sequence lookup and detectable overwrite. It is informed by Firedancer Tango's mcache
structure, but it does not reproduce the mcache ABI, workspace model, platform fences, or naming.

This mechanism is metadata-only. It intentionally does not enter the current throughput or
ping-pong workloads: those workloads define completion in terms of payload handoff, which this ring
cannot provide until F2 adds payload storage and reuse semantics.

## Contract

One producer publishes `Metadata` values in contiguous, one-based sequence order. `Metadata` is a
standard-layout value containing an application signature, chunk location, byte length, and control
word; its fields do not contain or determine the publication sequence. Capacity is a compile-time
power of two, and sequence `n` maps to slot `(n - 1) & (Capacity - 1)`.

`try_publish` returns the assigned sequence, or no value once the sequence range is exhausted.

The producer never consults consumers and may overwrite older publications. Any number of consumers
may call `try_read` with independently owned positions. This is lossy broadcast observation, not Q2
reliable fan-out, work sharing, or internal consumer registration.

`try_read` returns:

- `success`: the requested sequence was copied and remained stable through validation;
- `not_yet_published`: that slot still represents an earlier generation or its initial state;
- `overwritten`: a later sequence has displaced the request;
- `retry`: the producer is currently replacing the mapped slot;
- `invalid_sequence`: the request is outside the finite one-based publication range.

Every failure leaves the caller's output unchanged. `available_range` returns one snapshot of the
oldest candidate and latest announced completed sequence. It supplies resynchronization inputs
without choosing whether a caller skips to the oldest candidate, the latest publication, or stops.
Continued publication, or the short interval before the producer advances the progress watermark,
can make that snapshot conservative immediately, so callers must still handle another `overwritten`
or `retry` result.

## Representation and publication

Each slot contains an atomic publication sequence and atomic fields corresponding to `Metadata`.
Sequence zero is the initial empty state. The maximum value of `Sequence` is reserved as the
in-progress state, leaving `1 .. max - 1` as finite publication sequences.

For each publication, the sole producer:

1. stores the in-progress value to the mapped slot sequence;
2. stores every metadata field;
3. stores the final publication sequence;
4. advances the separate latest-published sequence.

After publishing `sequence_limit()`, the producer rejects every further publication. It never wraps
the counter into either the empty or in-progress value. The producer API is single-thread-owned;
concurrent producer calls are outside the contract.

## Portable ordering argument

All mechanism atomics deliberately use the default sequentially consistent ordering. This is the
portable conservative baseline for an overwrite race involving several independent atomic fields.
Their single total order prevents a consumer from accepting newer field stores while both sequence
checks still observe the displaced sequence.

A consumer first checks the slot sequence, copies each atomic metadata field into a local value, and
checks the slot sequence again. Seeing the in-progress state causes `retry`. Seeing a later final
sequence causes `overwritten`. When both checks observe the request, no replacement publication's
field stores can fall between them in the sequentially consistent order without one sequence check
also observing its preceding in-progress store or final sequence. Output is assigned only after the
second successful check. The latest-published store follows the final slot sequence store in the
same total order, so its snapshot never announces a publication before that slot is complete.

The atomic field representation avoids a C++ data race even when a producer overwrites a slot while
a consumer copies it. This mechanism does not claim that these atomics are lock-free on every
platform.

## Progress and limits

Publishing is bounded work and never waits for a consumer. Reading is one bounded attempt; callers
choose whether to retry. A consumer can lose any number of publications when the producer advances
more than the ring capacity beyond its position. No payload storage, copying, chunk validation,
backpressure, blocking wait, batching, or shared-memory lifecycle is provided.

Tests cover initial and future reads, direct mapping, ordered publication, independent observers,
exact-wrap overwrite, range snapshots, failed-output stability, finite exhaustion with an 8-bit
sequence type, and concurrent overwrite races that reject torn successful snapshots.
