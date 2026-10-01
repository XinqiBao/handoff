# Experiment: Lossy sequence-payload behavior under offered load

- Type: mechanism isolation
- Status: complete
- Plumbing revision: `3bf8d237a486efa22706f6836d30a8da744aabec`
- Measurement revision: `c53803448057fec431cd8262e3cdeb3868c0da7a`
- Date: 2026-09-15

## Question

How do unpaced pressure, explicit producer pacing, and periodic observer stalls change the observed
and overwritten shares of a bounded lossy sequence-payload ring?

## Hypothesis

An unpaced producer may expose sustained imbalance if publication outruns observation. A paced
producer should be fully observed when the observer remains active. At a measured 100,000 offers/s,
stalls longer than the 1024-slot ring's roughly 10.24 ms replacement window should create
repeatable overwrite episodes whose share grows with stall duration.

## Setup

The controlled experiment ran on the physical four-core Intel N150 host under Ubuntu 26.04.1 with
Clang 21.1.8 and a fresh native Release build. The exact clean measurement revision passed all five
required CI jobs, then passed fresh Linux Release and clang-tidy builds, format-check, and all 120
tests in each configuration. The process was restricted to coordinator CPU 0; producer and
observer placement was applied and verified on physical CPUs 1 and 2 in the same NUMA node. The
stock `intel_pstate` `powersave` governor, `balance_performance` EPP, boost state, and perf policy
4 were unchanged.

Only `sequence-payload` was used, with a 64-byte payload and 1024 slots. A bounded no-stall pilot
used nominal intervals of 0, 1,000, 10,000, and 100,000 ns. Their actual offered rates were
approximately 1.835 million, 1.000 million, 100,000, and 10,000 offers/s, and every pilot offer was
observed. The formal paced setting was therefore fixed at 10,000 ns: it was a clearly different
load regime from unpaced pressure while remaining short enough for repeated runs. At that rate the
ring capacity covers approximately 10.24 ms of publication.

A second two-row pilot fixed a stall after every 1024 successful observations. Stalls of 20 ms and
50 ms produced 49.824% and 78.496% overwritten shares, so both were retained as distinct formal
episodes without expanding the matrix. The formal settings were:

- unpaced, no stalls, 4,000,000 timed and 400,000 warmup offers;
- 10,000 ns pacing, no stalls, 300,000 timed and 30,000 warmup offers;
- the same pacing and counts with a 20 ms stall every 1024 successful observations;
- the same pacing and counts with a 50 ms stall every 1024 successful observations.

A 200,000,000-message basic-throughput command conditioned the package before six rotating blocks.
Each block ran every setting once with one trial, retaining six rows per setting and all 24 rows.
The shortest timed observation window was 1.980 seconds. Every row satisfied
`offered = observed + overwritten`; observed bytes equaled 64 times observed messages; every
success passed metadata and payload validation before contributing to its checksum; all affinity
metadata matched; every benchmark stderr was empty; and thermal-throttle counters did not change.

## Changed variable

The unpaced and paced no-stall settings change only the producer interval. The two stall settings
hold the 10,000 ns producer interval and 1024-observation stall interval fixed while changing only
stall duration. Payload generation, overwrite resynchronization, retry handling, warmup shape,
final drain, timing window, placement, build, host policy, and result accounting remain fixed.

## Results

The primary summaries pool all six formal rows for each setting. Shares are percentages of the
fixed offered count. Rates share the complete timed observation window through final drain.

| Setting | Median offered/s | Median observed/s | Observed share | Overwritten share | Median retries |
| --- | ---: | ---: | ---: | ---: | ---: |
| Unpaced, no stall | 1,837,024 | 1,837,024 | 100.000% | 0.000% | 406,387,307 |
| 10 us, no stall | 99,999 | 99,999 | 100.000% | 0.000% | 32,626,079 |
| 10 us, 20 ms stall | 99,952 | 49,470 | 49.493% | 50.507% | 114,996 |
| 10 us, 50 ms stall | 99,802 | 20,439 | 20.480% | 79.520% | 99,095 |

Unpaced offered-rate sample CV was 4.309% with an 11.432% full range, driven largely by a faster
first block; its delivery share was nevertheless 100% in every row. The paced no-stall offered rate
and share were effectively constant at the shown precision.

For the 20 ms stall, median observed share was 49.493% with 0.048% sample CV and a 0.119% full range
of the median; median offered-rate CV was 0.322%. The 50 ms setting observed exactly 61,440 of
300,000 offers in every row, giving the same 20.480% observed share six times; offered-rate CV was
0.008%. Working output was collected under ignored `results/l1/l1d-offered-load/`.

## Interpretation

Unpaced pressure did not outrun the observer in this configuration. The ring is lossy by contract,
but loss is conditional rather than inevitable: the observer validated every offer at a median
aggregate rate of 1.837 million/s. The paced no-stall result confirms full observation at the
selected lower rate; it does not demonstrate a share improvement because the unpaced share was
already 100%.

Periodic stalls created stable, bounded overwrite episodes. At approximately 100,000 offers/s, a
20 ms stall is about twice the ring's nominal replacement window and left roughly half of offers
observable. A 50 ms stall is about five times that window and left 20.480% observable. The direction
and magnitude were highly repeatable for these settings, supporting the expected relationship
among actual offered rate, capacity, and observer absence. Scheduler timing, wakeup delay, active
observation work, and final drain prevent treating those ratios as an exact analytical formula.

`retry_attempts` counts only snapshots rejected as unstable while a slot is being rewritten; it
does not count `not_yet_published` polls, which yield. The much larger no-stall retry totals are
therefore evidence of repeated overlap with publication, not dropped messages or producer
backpressure. Retry count is retained as mechanism behavior but is not combined with delivery share
into a score.

## Limitations

Portable `sleep_until` and `sleep_for` calls do not promise exact nanosecond scheduling. The
experiment covers one CPU, compiler, payload, capacity, producer/observer placement, stall interval,
and bounded set of load shapes. It records aggregate accounting and rates over a common window, not
per-publication latency, arbitrary arrivals, multiple observers, or cross-NUMA behavior. It does
not compare this lossy broadcast contract with a lossless backpressured queue and does not establish
a universal overload threshold.

## Reproduction

From a clean detached checkout of the measurement revision after fresh Release and tidy gates:

```sh
bench=./build/release/apps/handoff-bench/handoff-bench
result_dir=results/l1/l1d-offered-load
mkdir -p "$result_dir"

taskset -c 0 "$bench" run throughput \
  --implementation basic --payload-bytes 64 --capacity 1024 \
  --iterations 200000000 --warmup 2000000 --trials 1 \
  --producer-cpu 1 --consumer-cpu 2 --output "$result_dir/precondition.csv"

run_setting() {
  block=$1
  position=$2
  setting=$3
  case "$setting" in
    unpaced) interval=0; every=0; stall=0; iterations=4000000; warmup=400000 ;;
    paced) interval=10000; every=0; stall=0; iterations=300000; warmup=30000 ;;
    stall20) interval=10000; every=1024; stall=20000000; iterations=300000; warmup=30000 ;;
    stall50) interval=10000; every=1024; stall=50000000; iterations=300000; warmup=30000 ;;
  esac
  taskset -c 0 "$bench" run offered-load \
    --implementation sequence-payload --payload-bytes 64 --capacity 1024 \
    --producer-interval-ns "$interval" \
    --consumer-stall-every "$every" --consumer-stall-ns "$stall" \
    --iterations "$iterations" --warmup "$warmup" --trials 1 \
    --producer-cpu 1 --consumer-cpu 2 \
    --output "$result_dir/block${block}-${position}-${setting}.csv"
}

run_block() {
  block=$1
  shift
  position=0
  for setting in "$@"; do
    position=$((position + 1))
    run_setting "$block" "$position" "$setting"
  done
}

run_block 1 unpaced paced stall20 stall50
run_block 2 paced stall20 stall50 unpaced
run_block 3 stall20 stall50 unpaced paced
run_block 4 stall50 unpaced paced stall20
run_block 5 unpaced stall50 stall20 paced
run_block 6 stall50 stall20 paced unpaced
```

The retained sidecar records the exact host and load state, pilot decisions, verified placement,
stock policy, temperature/frequency observations, and throttle-counter delta.
