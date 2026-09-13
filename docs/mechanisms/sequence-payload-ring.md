# Sequence-Addressed Metadata/Payload Ring

## Purpose

`SequencePayloadRing<Capacity, ChunkSize, Sequence>` combines sequence-addressed publication
metadata with ring-owned, chunk-addressed payload storage. It preserves F1's direct lookup, caller-
owned observer positions, overwrite delivery, and explicit loss detection while adding a complete
copy-in/copy-out message operation.

The structure is informed by Firedancer Tango's separation of mcache metadata and dcache payload,
but it is not compatible with either facility. It has no workspace, allocator, topology, IPC, or
platform-specific fence surface.

## Layout and capacity

Capacity is a compile-time power of two. Every publication slot owns one fixed `ChunkSize` payload
chunk, so the exact logical payload capacity is `Capacity * ChunkSize` bytes. Sequence `n` maps both
to slot and chunk `(n - 1) & (Capacity - 1)`. Metadata records that chunk index explicitly even
though this first layout makes the mapping deterministic.

Logical payload length may vary from zero through `ChunkSize`. Bytes beyond that length in a reused
chunk are retained but unspecified and are never returned. On a successful read only the logical
payload prefix is copied; the caller's remaining output suffix is unchanged. This is direct chunk
addressing into separate owned storage, not variable-size byte-ring reservation, external payload
ownership, or a `zero-copy` interface.

## Operations and delivery

One producer calls `try_publish(signature, control, payload)`. It returns `success`,
`payload_too_large`, or `sequence_exhausted`. Invalid input neither changes a slot nor advances the
sequence. After publishing the finite `sequence_limit()`, further valid publications are rejected.

Any number of consumers independently call `try_read(sequence, metadata, payload)`. Results are
`success`, `not_yet_published`, `overwritten`, `retry`, `invalid_sequence`, or `output_too_small`.
Metadata and payload output remain unchanged on every failure. As in F1, `available_range` provides
a conservative oldest candidate and latest announced publication without choosing a skip, latest,
or stop policy for the caller.

The producer never reads consumer progress and never waits. A publication may overwrite a slow
observer's requested record. This is lossy broadcast observation, not reliable fan-out, work
sharing, or backpressure.

## Publication, reuse, and ordering

Each slot's sequence, metadata fields, and every payload byte are atomic. Sequence zero is empty and
the maximum sequence value is the in-progress sentinel. All mechanism atomics use the default
sequentially consistent ordering as a conservative portable baseline; they are not claimed to be
lock-free on every platform.

The producer marks the mapped slot in progress before changing any byte that the displaced metadata
can reference. It then stores the logical payload, signature, deterministic chunk index, length, and
control before storing the final sequence. The latest-published watermark advances only after that
final store.

A reader first checks for its requested sequence, copies metadata to a local value, and checks the
sequence again before reporting an undersized output. For a sufficiently large output it copies the
logical payload through atomic loads into a local fixed-size snapshot, then validates the sequence
again. Only a stable request assigns caller metadata and payload.

In the single sequentially consistent order, observing any replacement metadata or payload store
places the producer's earlier in-progress sequence store before the corresponding consumer load.
The consumer's later sequence check therefore cannot still accept the displaced sequence: it sees
either in-progress or the newer final sequence. Atomic payload representation also prevents the
underlying read/write race from being undefined before validation rejects the snapshot.

## Scope and coverage

The mechanism performs no dynamic allocation, cross-chunk record, variable chunk reservation,
blocking wait, batching, internal consumer registration, or shared-memory lifecycle management.
Tests cover exact capacities, zero and maximum payloads, oversized input, direct chunk mapping,
future and invalid reads, exact-wrap overwrite, output sizing and stability, independent observers,
range-based resynchronization, finite exhaustion, and mixed-length concurrent overwrite integrity.

The existing throughput and ping-pong workloads count every offered message as a required completed
handoff. Adding hidden producer gating or retries would change this mechanism's delivery semantics.
Benchmark integration therefore waits for W1 to report offered, observed, and overwritten
publications explicitly. Development and hosted CI timing remains plumbing evidence only.
