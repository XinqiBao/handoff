# Descriptor/payload SPSC ring

## Purpose and isolated difference

`DescriptorPayloadRing<DescriptorCapacity, PayloadByteCapacity>` separates fixed descriptor slots
from variable-length payload bytes. A descriptor contains the shared logical `RecordHeader`, a
physical payload offset, and the complete byte reservation released with that record. Consumers
must read a descriptor before locating payload, while the header no longer occupies the byte ring.

This mechanism remains a lossless, bounded, copy-in/copy-out SPSC queue. It does not expose storage
views, externalize payload ownership, publish descriptors without payload, provide sequence-addressed
lookup, broadcast, or overwrite unread data. It is not compatible with Firedancer mcache or dcache.

## Independent capacities and layout

The descriptor array has exact positive power-of-two compile-time slot capacity. Wrapping
descriptor head and tail positions distinguish empty from full without reserving a sentinel slot. Each private descriptor
stores a `RecordHeader`, a 32-bit payload offset, and a 32-bit reservation length. Descriptor layout
is private and no object-size or ABI promise is made.

The separate payload buffer has a power-of-two byte capacity, at least 32 and no greater than
`UINT32_MAX`. Non-empty payload starts and footprints are aligned to 16 bytes; the footprint is
`align_up(payload_length, 16)`. A zero-length payload consumes a descriptor but has a zero-byte
footprint. Alignment bytes and skipped end gaps are unspecified and never copied out.

One payload footprint is limited to half the byte capacity. If a non-empty payload does not fit in
the physical suffix, the producer includes the complete suffix in that descriptor's reservation and
stores the payload at offset zero. This bound guarantees that a valid payload can progress from any
aligned offset when the byte ring is empty. Unlike `VariableRecordRing`, no in-band padding header
is stored or parsed.

`payload_footprint(payload_length)` returns the aligned byte footprint for representable lengths and
no value otherwise. `descriptor_capacity()` and `payload_byte_capacity()` report the two native
limits; neither is an effective-message-capacity conversion.

## Operations and atomic reservation

`try_push(header, payload)` returns the mechanism-local `PushResult` values `success`, `full`, or
`invalid_record`. Header length must equal the input span size, and one payload must fit the
per-record bound. All `uint32_t` type tags are caller-visible because this mechanism needs no
padding sentinel.

A push admits the descriptor slot and the complete byte transition, including any end gap, before
copying. `full` means at least one native capacity is currently unavailable. Failed full or invalid
pushes publish neither resource and leave queue and caller state unchanged. A successful push copies
payload bytes, writes the descriptor, advances the producer-private byte tail, and then publishes
exactly one descriptor.

`try_pop(header, payload)` returns the mechanism-local `PopResult` values `success`, `empty`, or
`output_too_small`. Empty and undersized operations consume neither resource and modify neither
output. A successful pop copies the payload and header before releasing the descriptor's complete
byte reservation and its descriptor slot. One producer owns pushes, one consumer owns pops, and the
ring outlives both threads.

## Publication, reuse, and memory ordering

The producer acquire-loads descriptor head before reusing a slot and payload head before reusing
bytes. Payload and descriptor writes are sequenced before a release-store of descriptor tail. The
consumer acquire-loads descriptor tail before reading either region, so descriptor publication is
the sole visibility edge for a complete record.

After copying, the consumer release-stores payload head and then descriptor head. The producer reads
descriptor head before payload head. Observing the newer descriptor head through its acquire load
therefore also orders the earlier payload-head release before the subsequent payload-head load;
observing only newer payload progress can merely produce a conservative descriptor-full result.
Stale observations cannot authorize premature reuse of either resource.

Unsigned descriptor and byte positions may wrap. Each capacity is no greater than half the counter
range, so modular subtraction remains unambiguous while the ring obeys its bounded occupancy
invariant. Descriptor slot capacity must also be a power of two so modulo mapping remains
continuous at machine-counter rollover; bounded distance alone is insufficient for arbitrary
capacity. See the [basic SPSC mapping argument](basic-bounded-spsc.md#representation-and-capacity).
Physical payload offsets already use power-of-two masking. The
[boundary suite](../../tests/counter_rollover_test.cpp) exercises descriptor-counter rollover and
byte-counter rollover with a physical payload gap and delayed release.

## Correctness and benchmark coverage

Tests cover aligned footprint bounds, zero-length payloads, descriptor-only and byte-only full
states, invalid input, unchanged failed output, exact byte reuse, wrap-gap atomicity, FIFO integrity,
reserved-type availability, and a long concurrent mixed-length run.

The `descriptor-record` throughput and ping-pong modes validate the same sequence, type, payload
length, bytes, and checksum as the other record mechanisms. Supported capacity pairs are 64
descriptors with 4096 payload bytes and 1024 descriptors with 65536 payload bytes. CSV records both
native dimensions. Development and CI smoke timings are plumbing evidence only.
