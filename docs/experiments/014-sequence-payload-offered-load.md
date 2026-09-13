# Experiment: Lossy sequence-payload behavior under offered load

- Type: mechanism isolation
- Status: planned
- Revision:
- Date:

## Question

How do unpaced pressure, explicit producer pacing, and periodic observer stalls change the observed
and overwritten shares of a bounded lossy sequence-payload ring?

## Hypothesis

An unpaced producer will expose sustained imbalance when publication outruns observation. Moderate
pacing should increase the observed share, while periodic observer stalls should create repeatable
overwrite episodes whose size depends on capacity, pacing, and stall duration. Development and
hosted-CI timings cannot establish these effects.

## Setup

Follow the repository benchmark methodology on a controlled Linux host. Use one producer and one
observer on explicit same-NUMA CPUs, verify effective affinity, and preserve every raw trial. Keep
the compiler, build, payload size, capacity, warmup, offered count, trial count, and system state
fixed while varying one load-shape dimension.

The timed window ends only after final drain. Check every row for
`offered_messages = observed_messages + overwritten_messages`; retain retry counts, observed bytes,
checksum, and both rates even when the observed share is small.

## Compared variants

- unpaced pressure: producer interval zero and stalls disabled;
- producer pacing: positive producer interval and stalls disabled;
- temporary observer stalls: fixed producer interval plus a fixed positive observation interval and
  stall duration.

Use only the `sequence-payload` mechanism. This isolates load shape within its lossy delivery
contract rather than comparing it with lossless queues.

## Results

No controlled measurements have been recorded.

## Interpretation

No performance interpretation is available before controlled results exist.

## Limitations

Portable sleeps and scheduler wakeups do not provide exact nanosecond pacing. The workload records
aggregate accounting and effective rates over one observation window; it does not collect
per-publication latency, histograms, arbitrary arrival distributions, or multiple observers.

## Reproduction

Use the Release preset and the three command shapes in
[Reproducibility](../reproducibility.md). Replace representative CPU and load settings with the
controlled protocol recorded for the measurement host.
