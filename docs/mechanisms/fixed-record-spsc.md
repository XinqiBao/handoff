# Fixed-record SPSC ring

## Purpose and isolated difference

`FixedRecordRing<PayloadCapacity, Capacity>` is the first record-layout mechanism. Each slot holds a
concrete `FixedRecord<PayloadCapacity>` with an explicit fixed-width header followed by inline byte
storage. It retains the basic SPSC ring's ownership, exact slot capacity, copy-in/copy-out API, and
conservative memory ordering so the intended difference is the stored record layout and validation.

This is not a general record-storage abstraction. It does not pack variable physical record sizes,
use wrap markers, separate descriptors from payload storage, expose reservation tokens, allocate
payloads, or add batching, overwrite, or additional producers or consumers.

## Record layout

`FixedRecordHeader` is standard-layout and contains, in order:

- a `std::uint64_t sequence` at byte offset 0;
- a caller-visible `std::uint32_t type_tag` at byte offset 8;
- a `std::uint32_t payload_length` at byte offset 12.

The header is 16 bytes. `FixedRecord<N>` is also standard-layout, and its `std::array<std::byte, N>`
payload begins at byte offset 16. The record may have implementation-defined trailing padding when
needed for its alignment; there is no padding between its header and payload. The payload capacity
and ring slot count are independent compile-time arguments.

Only `payload[0..payload_length)` has record-level meaning. Unused inline bytes remain stored and
are copied with the concrete record object, but callers must not interpret their prior or default
contents as payload. A record is valid only when `payload_length <= PayloadCapacity`.

## Operations, ownership, and capacity

`try_push(const value_type&)` first rejects an invalid logical length. If the ring is full, it also
returns false. Either failure publishes nothing, does not modify a slot, and does not modify the
input. A successful push copies the complete record into producer-owned free storage and then
publishes it.

`try_pop(value_type&)` returns false and leaves its output unchanged when the ring is empty. A
successful pop copies the complete published record into consumer-owned output before releasing
the slot for producer reuse. The ring performs no allocation and owns `Capacity` default-initialized
records for its lifetime.

Only one producer calls `try_push` and only one consumer calls `try_pop`; both remain on their owner
threads, and the ring outlives them. Monotonic unsigned `head` and `tail` counters address slots by
modulo. `tail == head` means empty and `tail - head == Capacity` means full, so every declared slot
is usable. Capacity is restricted to at most half the counter range to keep bounded distance
unambiguous across unsigned wrap.

## Publication and memory ordering

The producer reads its owned `tail` relaxed and acquire-loads `head` before reusing a slot. The
complete header and payload copy is sequenced before a release-store of the advanced `tail`. The
consumer acquire-loads `tail` before copying that record, so the header and logical payload bytes
are visible together.

The consumer reads its owned `head` relaxed, copies the record, then release-stores the advanced
`head`. The producer's acquire-load of `head` prevents it from overwriting the slot until that copy
has completed. These are the same conservative publication and reuse edges as the basic SPSC
baseline; the byte-oriented value does not justify weaker ordering.

## Correctness and benchmark coverage

Tests fix only contractual layout facts: header field offsets and size, payload offset, and the
record's standard-layout and trivially-copyable properties. Runtime tests cover initial state,
zero-length and full-length payloads, invalid-length rejection, unchanged output on empty, exact
capacity, FIFO ordering, repeated wrap and reuse, and long concurrent header/payload integrity.

The `fixed-record` throughput and ping-pong modes use the existing 8, 64, and 256 byte logical
payloads and 64 or 1024 exact slot capacities. Each record duplicates the benchmark sequence in its
header, sets a fixed type tag, and identifies the complete selected payload as meaningful. Consumer
work validates all three header fields and performs the same payload/checksum loop as the generic
fixed-slot baseline. CSV leaves `capacity_bytes` empty because slot count, not byte count, is this
mechanism's native capacity. Development and CI smoke timings are plumbing evidence only.
