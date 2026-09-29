# MPSC variable-record byte reservations

## Question and boundary

`mpsc::VariableRecordRing<DescriptorCapacity, PayloadByteCapacity, Sequence>` asks whether two
producers can reserve different-sized byte ranges in FIFO descriptor order, write those ranges
independently, and return from publication across an earlier hole without releasing either resource
too soon. The implementation admits claims through one short reservation mutex. The mutex does not
cover payload writes or completion. This is a semantic storage/lifetime study, not a performance
comparison or a general variable-record queue framework.

One consumer thread owns `try_observe` and every observation token; producers may call `try_claim`
concurrently. Producers and the consumer cooperate; an abandoned claim is not recovered. There is
no cancellation after a producer claim, multiple consumer ownership,
overwrite, dynamic storage, or externally managed payload lifetime. Each producer must publish its
claim exactly once before the token is destroyed; an active token's destructor terminates. The ring
outlives its tokens and threads.

## Representation and admission

Each claim consumes one descriptor and an aligned byte extent. `try_claim(length)` checks the
per-record bound, finite descriptor ordinal and byte stream, descriptor credit, and byte credit
before changing either claim cursor. Failure returns no token and reserves nothing; it may mean
invalid length, insufficient credit, or finite cursor exhaustion. A successful claim assigns one
descriptor ordinal and one byte extent within the same mutex critical section. Other producers may
reserve and write disjoint extents while the first remains unfinished. `payload()` exposes only the
caller's logical length; the producer writes it directly. `publish(type_tag)` fills the shared
`RecordHeader` with the claim ordinal, type tag, and length before marking the descriptor ready. The
mutable span is valid only until `publish()`; the producer must make no later access through it.

Descriptor capacity is exact, including zero-length records. Payload capacity is an independent
power-of-two byte count. Nonempty payload footprints round up to 16 bytes; zero length takes no
bytes. A payload footprint is at most half the byte capacity. If it exceeds the remaining physical
suffix, the claim owns that entire suffix as a gap and starts its payload at offset zero. The gap
and payload are charged together, never exposed separately, and both remain occupied until the
record is released. The half-capacity bound lets a valid record fit from any aligned offset when
the byte ring is empty. An unsuccessful claim never consumes only one of the two resources.

Ordinal `max(Sequence)` is reserved as exhaustion: positions `0 .. max - 1` are claimable and
ready tags use `position + 1`. The ring does not roll descriptor generations over. Its 64-bit byte
cursor also stops before adding a reservation that would overflow, while physical offsets use
capacity masking. A stale released-byte snapshot may reject a claim conservatively; an out-of-range
distance is treated as full. Neither cursor rolls over, so an old snapshot or ready tag cannot
validate a new generation.

## Visibility and reuse

A producer's byte writes and descriptor header write precede its release-store of the descriptor's
generation tag. The single consumer acquire-checks only its next ordinal; a later completed claim
may return across a hole but cannot be observed before the hole closes. Its observation provides a
const header and const payload span valid until final `release()`. Cancelling or destroying an
observation retries the same ordinal and returns no credit. The observation's references and spans
expire at `release()` or `cancel()`.

After its last payload read, the consumer release-stores the cumulative byte credit and then the
descriptor credit. A claimant acquire-loads both before writing. Descriptor reuse is permitted only
after the matching descriptor release; byte reuse is permitted only after the matching byte release.
The two loads can form a conservative mixed snapshot, but neither resource may be reused on the
basis of the other credit alone. The consumer releases records in descriptor order, so a released
record's charged gap and payload form a contiguous safe byte prefix. Physical reuse happens when a
later successful claim writes that mapped byte range, not when `release()` merely returns.

The reservation mutex serializes admission and can block or starve a claimant. An unfinished early
claim blocks FIFO observation and eventually bounded reuse; a later producer may still publish and
return while capacity remains. No operation-wide lock-free, wait-free, fairness, or recovery claim
follows. Direct spans avoid an extra ring-side copy but do not imply end-to-end zero copying.

## Evidence

Deterministic tests cover independent descriptor and byte fullness, zero-byte records, a later
publication across an earlier hole, a physically wrapped gap owned by that claim, final-read reuse,
observation cancellation, and finite ordinal exhaustion. Two concurrent producers and one consumer
exercise mixed lengths and repeated reuse. [Experiment 021](../experiments/021-mpsc-variable-record.md)
records the validation and limits. There is no timed benchmark route.
