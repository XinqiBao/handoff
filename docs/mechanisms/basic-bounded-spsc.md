# Basic bounded SPSC ring

## Purpose and scope

`BasicBoundedRing<T, Capacity>` is the readable fixed-slot baseline for one producer and one
consumer. It provides lossless backpressure through non-blocking `try_push` and `try_pop`
operations. It is an educational mechanism-isolation baseline, not a general queue abstraction.

The ring does not provide blocking waits, reservations, batching, broadcast, multiple producers or
consumers, variable-size records, or overwrite behavior. It does not cache the other thread's index
or attempt to isolate shared indices on separate cache lines.

## Representation and capacity

Capacity is a compile-time template argument. It is also the exact usable capacity: a ring declared
with `Capacity == 4` accepts four values before reporting full. The representation contains exactly
`Capacity` default-constructed inline slots plus monotonically increasing unsigned `head` and
`tail` counters. A slot is addressed by `counter % Capacity`; no slot is reserved as a sentinel.

The complete object storage is established during construction. Push and pop perform assignment
into existing slots and do not allocate. `T` must be default-initializable and assignable, and its
assignment and destruction must not race with access outside the ring. The ring itself must outlive
both participating threads and is neither copyable nor movable.

Unsigned counter wrap is defined by C++. Capacity is restricted to at most half of the counter
range so the bounded `tail - head` distance remains unambiguous, including across wrap.

## Ownership and invariants

Only the producer calls `try_push` and advances `tail`. Only the consumer calls `try_pop` and
advances `head`. An element is occupied exactly when its logical position is in `[head, tail)`.
Therefore:

- `tail == head` means empty;
- `tail - head == Capacity` means full;
- the producer may write only the slot at `tail % Capacity` when the ring is not full;
- the consumer may read only the slot at `head % Capacity` when the ring is not empty.

A failed push or pop changes no ring state. Successful operations preserve FIFO order. The caller
retains ownership of a value passed to a failed push; a failed pop leaves its output unchanged.

## Publication and memory ordering

The producer owns `tail`, so it reads that counter with relaxed ordering. It acquires `head` before
reusing a slot, assigns the value, then release-stores the advanced `tail`. The consumer's acquire
load of `tail` makes the assigned slot visible before it reads the value.

Symmetrically, the consumer owns `head`, reads it relaxed, moves the value out, then release-stores
the advanced `head`. The producer's acquire load of `head` prevents it from reusing that slot until
the consumer's read has completed. Every operation reads the remote counter; reducing those reads
or weakening these acquire/release pairs belongs in later, separately documented experiments.

## Correctness coverage

Tests cover empty and full behavior, exact usable capacity, FIFO order, wraparound, repeated
fill/drain cycles, message integrity, and a million-message concurrent run that detects loss,
duplication, reordering, and torn payload observations.
