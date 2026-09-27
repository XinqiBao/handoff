# Experiment: Independent MPSC completion and FIFO visibility

- Type: implementation comparison with untimed semantic diagnostic
- Status: complete
- Measurement revision: `4b8aa01baf6cad01bbfba60c4c935764112800ba`
- Date: 2026-09-27 UTC

## Question

Can a producer return from publication across an earlier unfinished claim while
one consumer preserves claim-order FIFO and physical slots remain gated by
release? Under one equivalent complete-handoff workload, how do a cooperative
completion count and per-slot availability compare with the ordered-tail and
serialized controls?

## Contracts and hypothesis

The serialized control admits one producer operation at a time. The ordered
ring allows concurrent claims and payload writes but makes a later `publish()`
wait at a shared tail. The RTS-inspired count route lets each finisher return
after an acquire-release completion RMW; a catching finisher advances a whole
completed claim group. The generation-tagged slot route lets each finisher
release-mark its slot and return; the consumer acquire-discovers its contiguous
ready prefix. All shared-ring routes grant unique bounded positions by CAS,
deliver claim-order FIFO, and reuse a slot only after consumer release. None
recovers an abandoned claim. These differences predict semantic progress, not
a rate ranking: claim CAS, shared count/tail traffic, per-slot tag traffic,
consumer polling, and cache placement all affect complete-route throughput.

## Correctness gate and setup

The exact clean revision passed all five jobs in CI run `36294019364`: Linux
and macOS Release, format/clang-tidy, ASan/UBSan, and TSan. A separate Linux
execution clone fetched the remote, detached at the measurement SHA, built
Release natively with Clang 21.1.8 (`-O3 -DNDEBUG`), and passed all 133 Release
tests. Mechanism tests use latches for an early hole and a newer outstanding
claim, plus full capacity, wrap/reuse, finite `uint8_t` exhaustion, and four
producer sustained integrity. The local sanitizer and quality presets also
passed; those executions supplement the documented C++ happens-before proofs.

The physical four-core Intel N150 host ran Ubuntu 26.04.1 and Linux 7.0.0-31,
one NUMA node and no SMT. `taskset -c 0` confined the coordinator to CPU 0;
producers requested and verified CPUs 1 and 2, consumer CPU 3. Every retained
CSV records the measurement SHA, `git_dirty=false`, and effective masks 1, 2,
3. Stock `intel_pstate` `powersave`, `balance_performance` EPP, 700-3600 MHz
limits, boost enabled, and perf policy 4 were unchanged. Recorded core throttle
counts did not change; thermal zone 2 was 69 C before and 68 C after. These
endpoints do not establish per-trial temperature or frequency. Interactive
processes and CPU 3 network softirqs remained possible confounders.

A 40-million-message serialized run conditioned the host. Each retained row
used 64-byte inline payloads, 1024 exact usable slots, 2 million warmup and 20
million measured messages, one trial per command. The three four-command
blocks were `S O C A`, `A C O S`, and `O A S C`: serialized, ordered, count,
and availability. All twelve timed rows lasted 2.746-5.985 seconds and
validated checksum `3086954737262792784`. The two producers each produced
half the messages. The consumer validated FIFO positions and payloads; its
final completed handoff is the rate numerator. Payload work, yield retries,
placement, and timed phase boundaries matched across routes.

## Untimed progress diagnostic

At 64 slots, producer 0 held the first claim while producer 1 claimed and
completed the other 63 payloads. The coordinator observed a rejected further
claim, no visible position, and no consumer completion before closing the
hole. The ordered tail rejected 63 nonblocking publication attempts and had
zero later returned calls. Count and slot routes rejected none and had 63
returned calls each. After the first owner published, all three routes
validated and released all 64 positions with checksum `14174018928408746452`.
The separate latch test holds a newer claim when the first hole closes: count
visibility still lags the completed group, while the slot consumer immediately
discovers the already-ready prefix. These are semantic facts, not timings.

## Throughput observations

Rates below are million completed handoffs/s. Each block lists execution
order, and the range includes all three rows of that route.

| Block | Row 1 | Row 2 | Row 3 | Row 4 |
| --- | ---: | ---: | ---: | ---: |
| 1: S O C A | S 4.200 | O 4.400 | C 5.722 | A 7.283 |
| 2: A C O S | A 7.187 | C 5.722 | O 4.326 | S 3.951 |
| 3: O A S C | O 4.406 | A 7.254 | S 3.341 | C 5.658 |

| Route | Median | Range |
| --- | ---: | ---: |
| Serialized | 3.951 | 3.341-4.200 |
| Ordered tail | 4.400 | 4.326-4.406 |
| Completion count | 5.722 | 5.658-5.722 |
| Slot availability | 7.254 | 7.187-7.283 |

All three blocks had `A > C > O > S`. Median gaps were +30.0% for count
versus ordered and +26.8% for availability versus count in this run. The
serialized range was wider than those of the other routes. These are rates
for complete implementations on one host and workload, not isolated costs of
an RMW, slot tag, or frontier discovery, and the gap sizes should not be
extrapolated. No capacity, payload, or producer-count sweep was run because
the canonical question was answered without a concrete confound requiring one.

## Interpretation and limits

Independent completion permits later publication-call return but cannot let
the FIFO consumer cross a hole. The count route additionally waits for a
whole completed claim group before updating visible position. Slot
availability lets the consumer find a ready prefix as soon as its earliest
hole closes. All routes retain bounded reuse through consumer release, and a
permanently stalled owner can eventually fill the ring. The two new designs
are retained because they expose distinct completion representation and
visibility progress even independent of the measured rate direction.

This run does not establish a general performance ranking or isolate why the
slot route was faster. The control differs in mutex admission and copy path;
the ordered route differs in publication waiting; count and slot routes differ
in metadata placement, producer coordination, and consumer checks. The N150
has no spare physical core for the coordinator beyond CPUs 0-3, and endpoint
sidecars cannot rule out transient interference. A preliminary 12-row run at
`827f155` preceded a source reporting fix; its raw files are kept locally but
are not used as formal evidence under the repository exact-SHA rule.

## Reproduction and raw data

Raw CSVs and host sidecars are preserved in ignored
`results/phase2/producer-completion-final/` in the execution clone and
authoritative checkout. The sidecars include revision, tracked state,
compiler, OS/kernel, topology, allowed CPUs, policy, load, processes,
softirqs, temperature, and throttle counters before/after the group.

From a clean detached checkout of the measurement revision after native
Release build and tests, run the three untimed probes with
`--implementation mpsc-ordered|mpsc-count|mpsc-slot --payload-bytes 64
--capacity 64 --output FILE`. The conditioning command was:

```sh
taskset -c 0 ./build/release/apps/handoff-bench/handoff-bench run throughput \
  --implementation mpsc-serialized --payload-bytes 64 --capacity 1024 \
  --iterations 40000000 --warmup 2000000 --trials 1 \
  --producer-cpus 1,2 --consumer-cpu 3 --output conditioning.csv
```

Retained commands used the same invocation with `--iterations 20000000` and
the twelve implementation values in order `serialized ordered count slot
slot count ordered serialized ordered slot serialized count`, writing one CSV
per block position. No CPU policy was changed for the experiment.
