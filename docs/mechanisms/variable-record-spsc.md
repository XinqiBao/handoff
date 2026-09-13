# Variable-record SPSC byte ring

## Purpose and isolated difference

`VariableRecordRing<ByteCapacity>` packs variable-length records into one owned circular byte
buffer. Each logical record retains the shared 16-byte `RecordHeader`, but its physical footprint
depends on payload length. Records never straddle the physical end; a padding header makes wrap
explicit.

This mechanism preserves lossless SPSC delivery, copy-in/copy-out ownership, and conservative
acquire/release ordering. It does not expose queue storage, separate descriptors from payloads,
allocate records, batch publication, overwrite unread data, or support additional producers or
consumers.

## Layout and capacity

The compile-time byte capacity is a power of two, at least 32, no greater than `UINT32_MAX`, and
therefore a multiple of the 16-byte record alignment. A normal physical footprint is
`align_up(sizeof(RecordHeader) + payload_length, 16)`. Alignment bytes after the logical payload
are stored but unspecified and are never returned to the caller.

`record_footprint(payload_length)` returns the aligned footprint for representable payloads and no
value for larger lengths, so querying the public helper cannot overflow `std::size_t`.

One record's physical footprint is limited to half the byte capacity, making the maximum logical
payload `ByteCapacity / 2 - sizeof(RecordHeader)`. This bound ensures that a valid record can make
progress when an empty ring's cursors are at any aligned physical offset: the required end padding
plus record footprint always fits in the complete buffer.

Monotonic unsigned head and tail positions count all occupied bytes, including alignment and
padding. Their difference is the occupied byte count; zero means empty and `ByteCapacity` means
full, so no sentinel byte or empty slot is reserved. Power-of-two masking maps positions to physical
offsets and remains continuous when the unsigned counters wrap.

## Padding and physical wrap

If the suffix from the producer's current offset cannot hold the complete next record, the producer
writes a `RecordHeader` with type tag `UINT32_MAX`. Its length field contains the complete suffix
size, and its sequence field is ignored. The normal record then begins at offset zero. The producer
publishes the padding and record together with one tail update; a failed push never exposes padding
alone.

The reserved padding type tag is not a caller-visible record type. The consumer skips a padding
header internally and returns only the following normal record. If the caller's output span is too
small, neither the marker nor the record is consumed and both output arguments remain unchanged.

## Operations and ownership

`try_push(header, payload)` returns the mechanism-local `PushResult` values `success`, `full`, or
`invalid_record`. An invalid record has a reserved type tag, a header length different from the
supplied span, or a payload beyond the per-record bound. Full and invalid pushes publish nothing
and modify neither the ring's logical state nor caller storage. A successful push copies the
logical header and payload into producer-owned free bytes before publication.

`try_pop(header, payload)` returns the mechanism-local `PopResult` values `success`, `empty`, or
`output_too_small`. Empty and undersized operations do not consume bytes or modify either output. A
successful pop copies one logical header and its payload prefix into consumer-owned storage, leaves
any unused output suffix unchanged, and then releases the complete physical footprint. One producer
owns pushes, one consumer owns pops, and the ring outlives both threads.

## Publication and memory ordering

The producer reads its tail relaxed and acquire-loads head before reusing bytes. Header, payload,
and any padding header writes are sequenced before a release-store of the advanced tail. The
consumer acquire-loads tail before parsing and copying, so the complete wrap transition and record
contents are visible together.

The consumer reads its head relaxed and release-stores the advanced head only after copying the
record. The producer's acquire-load of head prevents reuse until that copy completes. These are the
same publication and reuse edges as the fixed-slot SPSC baseline; byte packing does not justify
weaker ordering.

## Correctness and benchmark coverage

Tests cover footprint calculations, invalid records, unchanged failed outputs, exact byte capacity,
zero and maximum payloads, almost-full wrap, atomic padding publication, undersized output across a
marker, FIFO order, repeated reuse, and a long concurrent mixed-length run.

The `byte-record` throughput and ping-pong modes use the existing 8, 64, and 256 byte logical
payloads with 4096 or 65536 bytes of native storage. Consumer work validates the same sequence,
type, length, payload, and checksum as `fixed-record`. CSV populates `capacity_bytes` and leaves
`capacity_slots` empty. Development and CI smoke timings are plumbing evidence only.
