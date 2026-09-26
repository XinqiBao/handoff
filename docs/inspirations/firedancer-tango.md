# Firedancer Tango Inspiration

## Purpose

Firedancer Tango is relevant for studying compact metadata separated from payload storage,
sequence-based observation, consumer progress, and detectable overwrite. `handoff` will not port the
Firedancer workspace, topology, tile runtime, allocators, or shared-memory lifecycle.

## Relevant ideas

Tango separates recent fragment metadata in an `mcache` from payload bytes in a `dcache`. Fragment
metadata includes a sequence, application-defined signature, chunk location, size, control bits, and
diagnostic timestamps. The mcache maps sequences onto a fixed set of metadata entries, combining
ring-like reuse with direct sequence lookup.

Publishing a newer sequence displaces older metadata at the same location. Consumers can use gaps
and repeated sequence checks to detect an overrun or overwrite race. The dcache provides separately
managed, chunk-addressed payload storage; compact layouts can keep an individual fragment physically
contiguous near wrap. Separate `fseq` objects can communicate receiver progress for flow control.

These facilities support more than one reliability policy. They should not be summarized as if all
Tango channels were necessarily broadcast, lossy, or backpressured.

## Intentionally excluded

- compatibility with mcache, dcache, fseq, or Tango IPC layouts;
- architecture-specific publication fences copied into portable C++;
- workspace, topology, process, allocator, and networking infrastructure;
- unqualified lock-free or `zero-copy` claims.

## Use in handoff

The repository already has a sequence-addressed metadata ring and a chunk-addressed payload
extension with independent observation and detectable overwrite. Their lossy contract remains
distinct from reliable fan-out and from the variable-record byte ring. Further Tango-inspired work
needs a new question rather than repeating this implemented combination.

## Primary sources

- [Tango fragment metadata at a fixed revision](https://github.com/firedancer-io/firedancer/blob/de039cd7fc9f4714782ec7e3d47db3728903abc6/src/tango/fd_tango_base.h)
- [mcache at a fixed revision](https://github.com/firedancer-io/firedancer/blob/de039cd7fc9f4714782ec7e3d47db3728903abc6/src/tango/mcache/fd_mcache.h)
- [dcache at a fixed revision](https://github.com/firedancer-io/firedancer/blob/de039cd7fc9f4714782ec7e3d47db3728903abc6/src/tango/dcache/fd_dcache.h)
- [fseq at a fixed revision](https://github.com/firedancer-io/firedancer/blob/de039cd7fc9f4714782ec7e3d47db3728903abc6/src/tango/fseq/fd_fseq.h)
