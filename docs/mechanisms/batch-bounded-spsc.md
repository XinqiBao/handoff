# Batch bounded SPSC ring

## Purpose and isolated difference

`BatchBoundedRing<T, Capacity>` is a fixed-slot SPSC variant that adds fixed-count all-or-nothing
batch operations to the basic ring's scalar API. One successful batch operation checks capacity
once, assigns every element, and publishes one advanced counter. It isolates publication
granularity without cached remote indices, explicit cache-line separation, reservations, or
best-effort partial progress.

## Representation and payload lifetime

The representation, exact usable capacity, monotonic atomic head and tail, modulo slot addressing,
and default-constructed inline slots match the [basic bounded SPSC ring](basic-bounded-spsc.md).
Slots remain live for the ring's lifetime and are reused by assignment. The ring allocates no slot
storage dynamically, while `T` construction or assignment may allocate, release, or retain
resources.

Scalar `try_push` and `try_pop` retain the basic ring's copy/move behavior, including support for
default-initializable move-only values. `try_push_batch(std::span<const T>)` requires copy
assignment and does not consume its inputs. `try_pop_batch(std::span<T>)` requires move assignment
and replaces its outputs only on success.

## Batch semantics and invariants

A zero-length batch succeeds and changes no state. A batch larger than `Capacity` fails. Push fails
without assigning slots or advancing tail unless the complete input span fits in currently free
slots. Pop fails without reading slots, changing outputs, or advancing head unless the complete
output span can be filled. Successful batches preserve FIFO order across the physical wrap
boundary.

All-or-nothing describes the queue's capacity and availability decision. Like the scalar baseline,
the mechanism does not provide transactional rollback if `T` assignment throws.

Only the producer calls push operations and advances tail. Only the consumer calls pop operations
and advances head. Scalar and batch calls may be mixed by their owning thread while preserving the
same logical `[head, tail)` occupied range.

## Publication and memory ordering

A batch producer relaxed-loads its owned tail and acquire-loads head. If enough capacity exists, it
copy-assigns every input in logical order, then release-stores `tail + count` once. A consumer that
acquires that tail observes every slot assignment sequenced before the release.

A batch consumer relaxed-loads its owned head and acquire-loads tail. If enough items exist, it
move-assigns every output in logical order, then release-stores `head + count` once. A producer that
acquires that head cannot reuse any batch slot before all corresponding consumer reads complete.

Memory orders are unchanged from the conservative basic baseline; only the number of elements
covered by one publication changes.

## Correctness and benchmark coverage

The shared scalar FIFO, payload/lifetime, wraparound, and concurrent integrity contracts apply.
Batch-specific tests cover zero and oversized spans, exact capacity, insufficient space and data,
unchanged inputs and outputs on failure, wrapped FIFO transfer, resource-owning assignment, mixed
scalar/batch reuse, and concurrent batched integrity.

The throughput benchmark uses explicit fixed batch sizes. `basic` performs equivalent scalar calls
for each generated batch, while `batch` uses one all-or-nothing operation; total messages, payload
generation, observation, waiting behavior, and checksum work remain equal. Ping-pong remains a
scalar exchange and rejects `--batch-size` because it does not model a batch handoff.

## Measured observation

On the controlled Intel N150 configuration recorded in
[experiment 005](../experiments/005-batch-spsc-comparison.md), size 1 was inconclusive because
paired block directions disagreed. Size 4 showed a directionally repeated but variable +5.715%
median batch advantage, and size 16 showed a smaller +1.483% conditional advantage with every
paired block positive. These group-size-specific observations are not a publication-scaling curve:
changing the compile-time group size also changes the common workload's generation and validation
loop shape.
