# Testing Strategy

Correctness is the entry condition for benchmarking, not a conclusion inferred from benchmark
output. Tests should make each mechanism's concurrency and ownership contract explicit while
keeping the test infrastructure smaller than the mechanisms under study.

## Layers

### Compile-time and layout checks

Use focused `static_assert` checks for requirements that are part of a mechanism's contract, such
as valid capacities, supported payload operations, alignment, or intentionally separated state.
Do not freeze incidental object sizes or private layouts unless a comparison depends on them.

### Deterministic state-machine checks

Every bounded FIFO mechanism with matching semantics should cover:

- empty and full behavior;
- exact usable capacity;
- FIFO ordering and payload integrity;
- wraparound and exact-boundary transitions;
- repeated fill and drain;
- failed operations leaving queue state valid;
- reuse after empty and full states.

Common test helpers are appropriate for these shared invariants when they do not erase meaningful
semantic differences.

### Concurrent integrity checks

Run sufficiently long producer-consumer tests with deterministic values or sequence-tagged
payloads. Verify message count, order where promised, absence of duplication, and payload integrity.
Vary capacity and force repeated wraparound. These tests look for concrete failures; they do not
constitute a proof of the memory-order argument.

### Mechanism-specific checks

Add tests beside the mechanism for obligations introduced by its design:

- batch operations: all-or-nothing versus best-effort behavior, boundary sizes, ordering, and wrap;
- reservation APIs: exclusive ownership, publish visibility, release, and abandoned-operation
  rules;
- concurrent producer claims: unique reservation; reverse-order completion of adjacent claims;
  ordered visibility across a hole; closing the hole; full capacity with claims in flight; safe
  reuse across physical wrap; finite sequence behavior; and the declared fate of unfinished claims;
- competing consumer claims: unique ownership; reverse-order completion across an acquired-item
  hole; distinction between release-call return and reusable progress; full capacity with an owner
  stalled; prefix discovery after the hole closes while a newer owner remains unfinished; safe
  reuse across physical wrap; finite sequence behavior; and the declared fate of abandoned owners;
- variable records: size bounds, alignment, padding markers, exact-tail cases, and almost-full wrap;
- broadcast: independent progress, slowest-reader gating, and the declared reader lifecycle;
- dependency pipelines: role ordering, unavailable downstream observations, dependency release and
  cancellation, final-stage reuse gating, and independently paced stages;
- lossy delivery: explicit gap or overrun detection and rejection of stale data;
- descriptor/payload separation: descriptor integrity, payload bounds, reuse, and publication order;
- lossy metadata/payload separation: atomic overwrite safety, failed-output stability, exact skipped
  sequence accounting, and successful snapshot integrity across both storage regions.

Do not force mechanisms with different delivery or ownership semantics through one universal test
interface.

## Payload and lifetime contract

The current fixed-slot SPSC baselines default-construct their slots and reuse them through
assignment. Tests and mechanism notes must state the resulting payload requirements and observable
resource-retention behavior. Add focused move-only or resource-owning payload tests only when the
declared operations support them.

Do not silently generalize an existing educational baseline to arbitrary object lifetimes. If
explicit construction and destruction becomes an experiment, preserve it as a distinct mechanism
with its own contract and tests.

For rvalue insertion, a failed operation must not consume or modify the source unless the mechanism
explicitly documents different semantics.

## Harness and output checks

CLI integration tests should cover help and listing, representative valid commands, invalid enum
values and numeric ranges, incompatible options, output failures, and stable exit behavior. CSV
tests should validate column meaning, empty non-applicable fields, trial counts, metadata, and
proper escaping when textual fields are introduced.

Small benchmark runs validate workload synchronization, completion, checksum behavior, and output
plumbing. Their timings are never performance evidence.

## Tool roles

- ordinary Debug and Release tests validate supported configurations;
- ASan and UBSan detect memory and undefined-behavior defects;
- TSan is useful evidence for exercised executions but neither proves correctness nor necessarily
  understands every future low-level synchronization technique;
- compiler warnings and clang-tidy catch maintainability and API mistakes;
- clang-format enforces mechanical consistency.

Document concrete sanitizer limitations. Do not weaken a correct algorithm solely to silence a
tool, but require a reviewed memory-model explanation for any suppression or exclusion.

## Benchmark gate

A mechanism may enter comparative benchmarks only after its note defines semantics, ownership,
capacity, overflow behavior, and memory ordering; deterministic and concurrent tests cover the
relevant invariants; and the project's required checks pass. Experiment records must still separate
plumbing validation from controlled performance evidence.
