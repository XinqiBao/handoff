# Experiment: Consumer coordination and work sharing

- Type: complete-route implementation comparison with untimed semantic tests
- Status: complete
- Measurement revision: `2603ff4f4c11520609169e288b012526276dbd24`
- Date: 2026-09-27 UTC

## Question and routes

With one ordered producer and two competing consumers, how do unique
acquisition, processing completion, release-call return, producer-visible
reclamation, and physical reuse differ when owners finish out of order?
The serialized control holds a consumer mutex through acquisition, processing,
and release. The ordered-release ring shares a CAS claim cursor but waits for
each predecessor at release. The slot-completion ring shares the claim cursor
and marks exact-generation completion independently; the sole producer scans
the contiguous reusable prefix. Their exact contracts and C++ ordering proofs
are in the corresponding mechanism notes.

The fixed two-consumer complete-handoff throughput command uses exact 64 or
1024-slot capacity, 8, 64, or 256-byte inline payloads, one producer, and two
workers. All routes generate and validate the same position-derived bytes,
retry with yield, track every position once with a per-position atomic seen
count, and record each worker's acquisition count. Warmup and timed phases
have the same synchronization boundaries. The producer ends timing only after
both consumers release all work and it verifies the final reusable prefix.
The numerator is fully processed, producer-verified publications. Fan-out
delivers each position twice and is not an equivalent rate control.

## Deterministic facts

The latch tests publish three positions and hold the first owner. Serialization
prevents later consumer acquisitions while its mutex is held. Ordered release
permits later acquisitions and payload work, but later nonblocking release
attempts fail and their release calls wait. Per-slot completion permits both
later releases to return, while the producer still rejects further claims at
full capacity. When the first hole closes, the producer discovers the slot
route's completed prefix even with a newer owner unfinished. Another test
shows ordered release exposes only the prefix whose release callers have
actually advanced the shared cursor. Shared tests check unique delivery and
payload integrity over repeated physical wrap; route-specific tests repeat
holes near terminal `uint8_t` positions. No test infers worker fairness from
the observed distribution.

## Correctness gate and setup

The exact clean revision passed all five required jobs in CI run `36319801285`:
Linux and macOS Release, format/clang-tidy, ASan/UBSan, and TSan. The
authoritative checkout also passed Debug, Release, both sanitizer suites, and
clang-tidy. A separate Linux execution clone fetched the remote, detached at
the exact SHA, built native Release with Clang 21.1.8 (`-O3 -DNDEBUG`), and
passed all 146 Release tests. The three-owner holes remain untimed tests;
they are semantic facts, not rates.

The physical Intel N150 has four cores, one NUMA node, and no SMT. `taskset -c 0`
restricted the coordinator to CPU 0; the producer requested CPU 1 and the
two workers CPUs 2 and 3. Every retained CSV reports effective masks 1, 2,
and 3, `git_dirty=false`, and the measurement SHA. Stock `intel_pstate`
`powersave`, `balance_performance` EPP, 700-3600 MHz limits, boost enabled,
and perf policy 4 were unchanged. A 40-million-message serialized run
conditioned the host. The canonical commands used 64-byte inline payloads,
1024 exact usable slots, 2 million warmup and 20 million measured messages,
one trial per command. The three blocks were `S O A`, `A S O`, and `O A S`:
serialized, ordered release, and slot completion.

All nine canonical rows validated every position and payload, all worker
counts summed to 20 million, the producer verified the terminal reusable
prefix, and the commutative checksum was `4287388160`. Timed durations were
2.651-7.086 seconds. Host sidecars before and after the group captured
topology, allowed CPUs, compiler/kernel/OS, policy, load, processes,
temperature, softirqs, and throttle counts. Recorded throttle counts did not
change; thermal zone 2 was 57 C before and 60 C after. CPU 3 handled network
softirqs and interactive processes remained possible confounders. Endpoint
samples cannot establish each trial's frequency or absence of interference.

## Canonical observations

Rates are million producer-verified complete handoffs per second.

| Block | First | Second | Third |
| --- | ---: | ---: | ---: |
| 1: S O A | S 2.890 | O 6.660 | A 7.544 |
| 2: A S O | A 7.414 | S 2.823 | O 6.969 |
| 3: O A S | O 5.046 | A 7.340 | S 2.839 |

| Route | Median | Range |
| --- | ---: | ---: |
| Serialized | 2.839 | 2.823-2.890 |
| Ordered release | 6.660 | 5.046-6.969 |
| Slot completion | 7.414 | 7.340-7.544 |

Every canonical block had `A > O > S`, but the third ordered row was well
below its other two. Per-worker counts stayed near half of the work; that is
an observed distribution for these runs, not a fairness guarantee. The
serialized route cannot overlap consumer processing. Ordered release can
overlap it, yet later calls wait at the release cursor. Slot completion lets
later calls return independently and charges prefix discovery to the producer.
The rates include all these costs plus the common per-position validation;
they do not isolate an atomic, mutex, scan, or coherence cost.

## Targeted repeatability check

The low ordered row motivated one `O A A O` bracket at the same SHA, workload,
and placement. Ordered measured 7.016 and 7.006 million/s; slot measured
7.493 and 9.723 million/s. The ordered dip did not repeat in this bracket,
and one slot row was unusually high. The before/after bracket sidecars show
unchanged throttle counters and thermal zone 2 at 55 C at both endpoints.
They do not identify the source of either deviation. The sensitivity rows are
retained as evidence of variability, not folded into canonical medians or
treated as a stable effect-size estimate. No capacity or payload sweep was
run without a more specific causal hypothesis.

## Reproduction and raw data

Working output and host sidecars were collected under ignored
`results/phase2/consumer-coordination-final/`. From a clean detached checkout of the measured
SHA after native Release build and tests, the conditioning command was:

```sh
taskset -c 0 ./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation spmc-serialized --payload-bytes 64 --capacity 1024 \
  --iterations 40000000 --warmup 2000000 --trials 1 \
  --producer-cpu 1 --consumer-cpus 2,3 --output conditioning.csv
```

The nine retained commands used this exact loop. The four sensitivity commands
used the same arguments, implementation order `ordered slot slot ordered`, and
output names `sensitivity-1-ordered.csv`, `sensitivity-2-slot.csv`,
`sensitivity-3-slot.csv`, and `sensitivity-4-ordered.csv`.

```sh
bench=./build/release/apps/handoff-bench/handoff-bench
result_dir=results/phase2/consumer-coordination-final
variants=(serialized ordered slot slot serialized ordered ordered slot serialized)
for index in "${!variants[@]}"; do
  block=$((index / 3 + 1))
  position=$((index % 3 + 1))
  variant=${variants[index]}
  taskset -c 0 "$bench" run throughput \
    --implementation "spmc-$variant" --payload-bytes 64 --capacity 1024 \
    --iterations 20000000 --warmup 2000000 --trials 1 \
    --producer-cpu 1 --consumer-cpus 2,3 \
    --output "$result_dir/block${block}-${position}-${variant}.csv"
done
```

No CPU policy was changed.

## Limits

All mechanisms require cooperating owners to call release. An abandoned
owner can permanently block capacity. Exactly-once successful user effects,
fairness, recovery, leases, timeouts, cancellation after acquisition,
operation-wide lock-freedom, and MPMC composition are not established.
This is one host, two workers, one payload/capacity pair, one waiting policy,
and one placement. The observed rate order is conditional; the variable
ordered and slot rows leave their performance causes unresolved.

## Selection and current applicability

The three routes distinguish acquisition overlap, release-call return, and producer-discovered reuse.
A shared consumer completion count was skipped because it can hold a completed prefix behind a newer
unfinished claim without a distinct consumer-side question. Consumer helping and local turn reuse
were skipped because producer-only discovery resolves the hole, and one ordered cyclic producer
still cannot cross the earliest owned slot. Partitioned SPSC paths change FIFO merge authority.

These rates describe the measurement revision above. The later correctness campaign repaired the
ordered-release and slot-completion acquisition bound: consumers must acquire a publication bound
strictly greater than the position they CAS-claim. A newer shared claim cursor cannot substitute for
that payload visibility edge. The historical tests and rates did not establish correctness of every
portable C++ history, and the rates do not predict performance of the repaired implementations.
