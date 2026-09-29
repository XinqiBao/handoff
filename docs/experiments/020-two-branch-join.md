# Experiment 020: Independent branch writes at a fixed join

- Type: semantic topology study
- Status: complete
- Mechanism revision: `f79bf6b11c3c49b2112950a570c2cf6163d8a535`
- Date: 2026-09-29 UTC

## Question and contract

Can a fixed join observe two independently written results for the same position only after both
branches finish, while its own final release remains the sole permission for physical reuse? The
[fixed two-branch join](../mechanisms/two-branch-join.md) assigns producer input and each branch
result to separate slot subobjects. Each branch processes positions in FIFO order but can finish
ahead of the other. The join reads its next position only after acquiring both completion cursors.
The mechanism note owns the C++ happens-before proof and lifetime contract.

## Semantic evidence

A latch-controlled test holds the left owner of position 0 while the right branch completes
positions 0 and 1. The join cannot acquire position 0, and the exact-capacity producer cannot
claim another slot. After left completes 0, the join can acquire only 0; it still cannot cross
unfinished left position 1. Holding the join observation continues to block producer reuse.
Releasing it permits the producer to wrap onto the first physical slot. A separate test shows
that both branches may have completed two positions while capacity remains full until the join
releases one. Cancelling a join observation also leaves the slot occupied.

The finite-position test processes all 255 claimable `uint8_t` positions and rejects rollover.
The concurrent test runs one producer, two branch owners, and one join consumer through 30,000
positions in seven slots. Each branch checks producer input; the join checks both distinct results
and position order. This supports integrity for the exercised cooperative execution, not fairness
or recovery after an owner stops.

## Interpretation and limits

The two branch publication edges into one join are the new invariant. Read-only fan-out only needs
to account for the slowest reader before reuse, while the prior pipeline has one serial dependency
path. This study makes both branch results visible through separate acquire loads and defers reuse
until the join's last access. Branches themselves are ordered, so the test does not establish
out-of-order completion within a branch. No benchmark was added: semantic tests resolve the stated
question, and the N150's four cores provide no spare core for a clean four-role run plus coordinator.
Local Clang/C++23 Debug, Release, ASan/UBSan, TSan, and tidy builds passed; all 162 tests passed
in each preset, and the format check passed. The exact mechanism revision passed CI run
`36511943487`, including Linux and macOS Release, sanitizers, formatting, and clang-tidy.
