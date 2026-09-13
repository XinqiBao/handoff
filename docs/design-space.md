# Design Space

This document is the stable vocabulary for classifying mechanisms and experiments. It is not an
implementation schedule and does not imply that every combination should be built.

## Experiment classes

A **mechanism-isolation experiment** changes one important design property while holding relevant
workload and harness behavior as constant as practical. An **implementation comparison** compares
representative implementations whose differences may not be reducible to one property. Results
must identify which class applies.

The goal is conditional understanding: which mechanism behaves well under which conditions, why,
and with which semantic or structural trade-offs. The project does not seek a universal ranking.

## Producer and consumer topology

- **SPSC**: one producer and one consumer.
- **MPSC**: multiple producers and one consumer.
- **SPMC**: one producer and multiple consumers.
- **MPMC**: multiple producers and multiple consumers.

Topology alone does not define delivery. Multiple consumers may share work or independently observe
the same publications.

## Delivery model

- **Work sharing**: each message is consumed by one eligible consumer.
- **Broadcast (fan-out)**: independent consumers observe each required message.

Consumer dependency graphs may later constrain when a downstream consumer can advance.

## Overflow and delivery semantics

- **Lossless backpressure** prevents publication when required capacity is unavailable.
- **Slowest-reader gating** prevents overwrite until every required reader has advanced.
- **Overwrite (lossy delivery)** permits newer publication to displace older data and requires
  detectable overruns.

Mechanisms need not provide identical semantics. A stale or overwritten entry must never be silently
interpreted as valid.

## Progress tracking

Candidate representations include head/tail indices, monotonic global sequences, per-slot
sequences, consumer gating sequences, and separate reservation/commit state. These terms are not
synonyms: documentation should name the state and the invariant it carries.

## Ownership and publication

Relevant operation shapes include:

- push/copy into queue-owned storage;
- direct write into queue-owned storage;
- `reserve -> write -> publish`;
- `consume -> release`.

Use these precise descriptions instead of loosely claiming `zero-copy`. Reservation and publication
may introduce obligations that a simple push operation does not have.

## Storage layout

- **Fixed inline slot**: each ring slot contains a fixed-capacity value or record.
- **Variable record byte ring**: aligned `[header][payload]` records occupy one circular byte buffer.
  The initial baseline should keep records physically contiguous and use a padding or wrap marker
  when the tail cannot fit a complete record.
- **Descriptor ring with separate payload storage**: compact metadata references bytes held in a
  distinct payload region. Consumers may inspect descriptors without touching payload.

The latter two layouts are conceptually distinct and should remain separate mechanism families.

## Payload model

Experiments may use fixed-size payloads, variable-size payloads, or mixed distributions. Useful
fixed sizes may include 8 B, 64 B, 256 B, and 1024 B, selected explicitly rather than swept
combinatorially by default.

## Publication granularity

Publication may occur per message or in a batch. Bulk APIs may also distinguish fixed-count success
from best-effort burst behavior; any such semantics must be named explicitly.

## Waiting strategy

Busy spin, CPU pause, yield, and later a justified blocking strategy are separate experimental
dimensions. Waiting behavior must not be hidden inside a mechanism comparison.

## Memory ordering

Each mechanism begins with a conservative correct baseline. Later variants may reduce
acquire/release operations or use relaxed ordering where an explicit happens-before argument and
tests support the change. C++ memory ordering must be reasoned about directly rather than copied
from another language or architecture.

## Workloads and dimensions

Fundamental workloads are steady-state throughput and ping-pong round-trip latency. Later workloads
may cover bursts, producer/consumer imbalance, temporary consumer stalls, and latency under offered
load.

The harness may grow toward these explicit dimensions:

- implementation;
- payload size;
- slot capacity or byte capacity;
- batch size;
- iteration count, warmup, and trial count;
- producer CPU and consumer CPU;
- fixed consumer count for broadcast workloads.

Not every option applies to every mechanism. Commands should reject nonsensical combinations rather
than force them through a universal configuration object. Default experiments should remain small;
broader sweeps should be explicit.
