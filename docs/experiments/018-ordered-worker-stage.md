# Experiment: Ordered worker-stage progress

- Type: semantic mechanism study with deterministic and concurrent integrity evidence
- Status: complete
- Mechanism revision: `0a05f4be6ba1e7551da1793bed8c2c751b23452d`
- Date: 2026-09-28 UTC

## Question and contract

With one ordered producer, two competing workers in one fixed processing stage, and one ordered
downstream consumer, can workers complete independently and out of order while downstream sees only
their contiguous completed prefix, and only downstream release permits physical reuse?

The [ordered worker-stage ring](../mechanisms/ordered-worker-stage.md) holds fixed inline slots.
The producer writes a position-tagged payload, publishes with release ordering, and workers claim
unique positions by CAS after acquiring publication. Each worker writes a deterministic transformed
field and independently release-tags its exact logical position. The sole downstream consumer
acquire-scans completion tags, reads each transformed payload in order, and release-advances the
reuse cursor after its last slot access. The mechanism note owns the full C++ ordering and lifetime
proof. Tokens require cooperative completion and release.

## Deterministic semantic evidence

The latch-controlled hole test publishes three positions. W0 acquires position 0 and waits before
completion. W1 acquires position 1, completes it, and signals that `complete()` returned. Downstream
still reports completed prefix 0 and cannot acquire either position. At full capacity the producer
cannot claim. W1 then owns position 2 and waits unfinished. When W0 completes, downstream alone
discovers prefix 2, consumes positions 0 and 1, and stops exactly before position 2. Once W1
completes position 2, downstream reaches prefix 3. This separates returned worker completion from
contiguous visibility, including the case of a newer unfinished position.

The exact-capacity test completes both worker positions and observes a full completed prefix, yet
rejects producer reuse. Holding a downstream claim continues to reject reuse. Releasing its first
position permits one producer claim, while the second remains owned. Repeated physical generations
are checked against exact completion tags: an old tag does not make a newly published position
visible. The `uint8_t` test processes all 255 claimable positions and rejects rollover. Initial
empty state, producer cancellation, mutable worker access, const downstream access, and move-only
ownership are also checked.

## Concurrent integrity evidence

One producer, two workers, and one ordered downstream thread process 30,000 positions in a
31-slot ring, repeatedly wrapping physical storage. Per-position atomic counters record exactly
one worker claim; downstream checks every position in order and verifies both producer position
and worker transformation. Terminal worker completion, stage prefix, and downstream release all
equal 30,000. This establishes the tested cooperative execution's integrity; it does not prove
fair scheduling, exactly-once external side effects, or failure recovery.

## Validation and interpretation

At the mechanism revision, local Clang/C++23 Debug, Release, ASan/UBSan, TSan, and clang-tidy builds
passed. All 151 tests passed in each preset; the format check passed. The exact core revision also
passed CI run `36445056707`: Linux and macOS Release, ASan/UBSan, TSan, and format/clang-tidy.
The deterministic tests are semantic evidence; sanitizers cover the executed interleavings. The
mechanism note gives the independent memory-model argument. The experiment has no timed data or
performance claim. A complete-route timing would add coordinator placement to four active roles
on the four-core N150, while no unresolved rate question remains after the semantic tests. No
benchmark integration or new result schema was justified.

The result answers the selected question for cooperative participants: a later worker can finish
and return across an earlier hole; downstream discovery constructs only the safe prefix; final
downstream release alone authorizes physical reuse. The existing SPMC slot-completion ring puts
prefix discovery at the producer for reclamation. The fixed pipeline gates ordered downstream
access but has one upstream owner, so it cannot construct this frontier from competing completions.
One mechanism suffices for this distinction. Worker-helped finalization, a completion count,
additional stages, and a timed comparison were skipped because they introduce no demonstrated
unanswered semantic question here.

An abandoned worker leaves a permanent hole and can eventually block bounded publication.
Fairness, cancellation after worker acquisition, recovery, arbitrary stage graphs, and general
MPMC composition remain outside this cooperative contract. No alternative mechanism is selected
as the next program by these results.
