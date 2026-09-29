# Experiment 019: Producer-owned paths and merge authority

- Type: semantic topology study
- Status: complete
- Mechanism revision: recorded by the `feat(topology)` commit containing this study
- Date: 2026-09-29 UTC

## Question and contract

How does moving coordination from a shared MPSC claim cursor to two producer-owned SPSC paths
change ordering, progress, backpressure, and slot reuse? The selected mechanism is the fixed
[two-path merge](../mechanisms/two-path-merge.md). It preserves FIFO within each path and makes the
consumer the merge-order authority. Preference rotates after a successful acquisition when both
heads are eligible. Capacity is exact and partitioned: two paths of `C` slots have `2C` total slots,
but neither producer can use its neighbor's spare capacity. An acquired direct-slot observation
must be released after the last read before that path may reuse it. The note owns the C++ ordering
argument and the progress and failure limits.

## Semantic evidence

Deterministic tests show that ready heads alternate and that first-path selection can follow a
second-path publication. An unpublished first-path reservation does not hide a ready second-path
head, and a held first-path observation permits second-path acquisition.
At full capacity, cancelling the first observation does not free its slot; releasing it does.
The second path wraps its single physical slot repeatedly while the first observation stays held.
These facts distinguish merge selection and path-local backpressure from the shared MPSC rings'
claim-order FIFO and cross-producer publication hole. They do not compare equal ordering or capacity
contracts.

The concurrent integrity test sends 50,000 tagged records through each seven-slot path and checks
each producer's sequence and inverse field at the merge. It establishes the tested execution's
per-path FIFO and payload integrity across repeated physical wrap; it does not prove scheduling
fairness or recoverability after an owner stops.

## Validation and limits

Local Clang/C++23 Debug, Release, ASan/UBSan, TSan, and static-analysis gates and CI are recorded in
the final campaign checkpoint. No timed comparison is made. The existing shared MPSC routes use a
single global FIFO and shared capacity; assigning equal total slots would still leave different
backpressure and ordering contracts. The N150 is useful for these semantic tests and plumbing, not
for an unsupported cache/coherence attribution.
