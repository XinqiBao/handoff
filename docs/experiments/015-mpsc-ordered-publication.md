# Experiment: Serialized ownership versus ordered MPSC publication

- Type: implementation comparison
- Status: complete
- Measurement revision: `ca821af5f80194c69db8271803316282e7def5bd`
- Date: 2026-09-26 UTC

## Question

With two producers and one FIFO consumer, how does whole-operation producer serialization compare
with concurrent claims and one ordered publication frontier under steady fixed-payload load? The
separate publication-hole diagnostic asks which later work can progress while the first claim is
unfinished; it does not provide a rate.

## Hypothesis

Concurrent claims can overlap payload work, but a finisher waits for earlier claims at the shared
publication frontier. A producer mutex removes that overlap and adds lock admission and ownership
cost. The net throughput direction is therefore an empirical property of these complete routes,
not a prediction from one atomic instruction.

## Setup

The exact clean revision passed all five required CI jobs in run `36263371290`: Linux and macOS
Release, format/clang-tidy, ASan/UBSan, and TSan. A separate Linux execution clone fetched the
remote and detached at that SHA, built Release natively with Clang 21.1.8 (`-O3 -DNDEBUG`), and
passed all 125 Release tests. The physical four-core Intel N150 host ran Ubuntu 26.04.1 and Linux
7.0.0-31, with one NUMA node and no SMT. `taskset -c 0` kept the coordinator on CPU 0; producers
requested CPUs 1 and 2 and the consumer CPU 3. Every retained CSV reports exact effective masks
1, 2, and 3, `git_dirty=false`, and the measurement SHA.

The stock `intel_pstate` `powersave` governor, `balance_performance` EPP, 700-3600 MHz limits,
boost enabled, and perf policy 4 were unchanged. A 40-million-message serialized run conditioned
the host. The retained configuration was 64-byte inline payloads, 1024 exact usable slots, 2
million warmup and 20 million measured messages per one-trial command. Three paired four-command
blocks used `S O O S`, `O S S O`, and `S O O S`, where S is `mpsc-serialized` and O is
`mpsc-ordered`. Every timed row lasted 3.697-7.128 seconds and every checksum was
`3086954737262792784`. The separate 2-million-message placement pilots were not retained as
performance evidence.

Host sidecars captured before and after the group include topology, current allowed CPUs,
compiler/kernel/OS, policies, load, active processes, temperatures, softirqs, and thermal
throttle counters. The recorded core throttle counts did not change. Thermal zone 2 was 70 C
before and 68 C after; these endpoints do not establish each trial's temperature or frequency.
CPU 3 also handled network softirqs, as in the historical host baseline. Raw CSV and sidecars
remain in ignored `results/phase2/mpsc-ordered/` in both the execution clone and authoritative
checkout.

## Compared routes

- **S (`mpsc-serialized`):** A mutex covers FIFO position assignment, payload generation, full
  retries, and `BasicBoundedRing::try_push`. Only one producer operation can be in flight.
- **O (`mpsc-ordered`):** CAS grants distinct bounded positions. Producers write directly into
  their claimed slots and advance one contiguous publication tail in claim order. Later finishers
  wait across an earlier hole. The consumer's release cursor controls reuse.

Both routes use two producers, one consumer, scalar fixed payload work, the same position-derived
payload bytes, the same validation/checksum, exact slot capacity, lossless delivery, yield retries,
and completed consumer handoffs as the rate numerator. They differ in mutex admission, CAS claim
traffic, slot assignment shape, publication waiting, and cache behavior. This is not a measurement
of the isolated cost of ordered publication.

## Results

Retained rates are million completed handoffs per second, in execution order:

| Block | Position 1 | Position 2 | Position 3 | Position 4 | O versus S block median |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1: S O O S | S 3.008 | O 4.707 | O 4.618 | S 3.190 | +50.5% |
| 2: O S S O | O 4.702 | S 3.705 | S 3.120 | O 4.650 | +37.0% |
| 3: S O O S | S 2.806 | O 5.410 | O 4.578 | S 4.263 | +41.3% |

Across six rows per route, the S median was 3.155 million/s and the O median was 4.676
million/s, a +48.2% median difference in this run. All six O rows exceeded all six S rows. The S
sample range was 2.806-4.263 million/s (sample CV 16.1%, full range 46.2% of its median); the O
range was 4.578-5.410 million/s (CV 6.6%, full range 17.8%). The publication-hole diagnostic at
64 slots independently reported 64 successful claims, 63 completed later payloads, 63 rejected
later publication attempts, no returned blocking publication, no visible or consumed position,
and a rejected further claim before the first producer completed. After it completed, all 64
positions were validated and released with checksum `14174018928408746452`. Its raw
`progress-64.csv` is retained beside the throughput rows. That diagnostic is an untimed semantic
observation.

## Interpretation

At this host, placement, payload, and workload, concurrent claims with an ordered tail delivered
more completed handoffs than whole-operation serialization in every retained row. The block
direction persisted when command order reversed. The large within-route variation, especially
for S, prevents treating +48.2% as a precise stable effect size. No evidence here isolates whether
the direction came from overlapped payload work, mutex contention, CAS behavior, direct slot
access, publication coordination, or their interactions.

The diagnostic establishes a separate progress trade-off. A delayed first owner does not prevent
later bounded claims or payload completion, but it prevents a contiguous visible tail and blocks
later `publish()` returns. Once capacity is charged, further claims fail until consumer release;
indefinitely delayed ownership would keep the hole without recovery. The serialized route permits
no later producer operation to enter while its mutex owner is delayed.

## Limitations

This is one machine, one role placement, one payload size/capacity, one waiting policy, and one
saturated two-producer workload. CPU 3 had network softirq activity, and unrelated interactive
processes remained on the host. The large S variation has no established cause; endpoint host
checks do not prove stable per-trial frequency or absence of short interference. The N150 has
exactly four cores, leaving no spare physical core beyond the coordinator and workers. Results
do not rank mechanisms generally, estimate publication-stall latency, or establish a benefit for
per-slot availability, helping, or any later Phase II mechanism.

## Reproduction

From a clean detached checkout of the measurement revision after native Release build and test:

```sh
cmake --preset release
cmake --build --preset release --parallel 4
ctest --preset release --no-tests=error

bench=./build/release/apps/handoff-bench/handoff-bench
result_dir=results/phase2/mpsc-ordered
mkdir -p "$result_dir"
taskset -c 0 "$bench" run throughput \
  --implementation mpsc-serialized --payload-bytes 64 --capacity 1024 \
  --iterations 40000000 --warmup 2000000 --trials 1 \
  --producer-cpus 1,2 --consumer-cpu 3 --output "$result_dir/conditioning.csv"
```

The twelve retained commands followed this exact order and invocation:

```sh
variants=(serialized ordered ordered serialized
          ordered serialized serialized ordered
          serialized ordered ordered serialized)
for index in "${!variants[@]}"; do
  block=$((index / 4 + 1))
  position=$((index % 4 + 1))
  variant=${variants[index]}
  taskset -c 0 "$bench" run throughput \
    --implementation "mpsc-$variant" --payload-bytes 64 --capacity 1024 \
    --iterations 20000000 --warmup 2000000 --trials 1 \
    --producer-cpus 1,2 --consumer-cpu 3 \
    --output "$result_dir/block${block}-${position}-${variant}.csv"
done
```

The benchmark validates and records requested/effective placement in each CSV. Capture the host
sidecar before conditioning and after the final row as described in
[Reproducibility](../reproducibility.md).

The separate untimed progress result was collected with:

```sh
"$bench" run publication-hole --payload-bytes 64 --capacity 64 \
  --output "$result_dir/progress-64.csv"
```
