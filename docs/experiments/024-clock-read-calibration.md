# Experiment: Clock-read floor on the isolated N150

- Type: measurement-method characterization
- Status: complete for this host and probe; benchmark clock unchanged
- Revision: `f9eea5152608d0fc5b1006f1958f186ac2195ffb`
- Date: 2026-10-01 (Asia/Shanghai)

## Question and method

Does a fenced TSC reader offer a lower read cost than `steady_clock` for nanosecond-scale
ping-pong RTT on this host? The probe ran on CPU 1 at fixed 2400 MHz, after a 20-second busy
qualification. Seven same-core TSC calibrations each spanned 250 ms. For each reader it retained
200,000 adjacent-read raw deltas, then timed ten million reads with an observable accumulator.
Readers were `steady_clock`, `CLOCK_MONOTONIC_RAW`, `LFENCE; RDTSC; LFENCE`, and
`LFENCE; RDTSCP; LFENCE`. The adjacent-read distribution includes the loop and serialization
behavior; the amortized loop has its own overhead. Neither directly measures the complete
ping-pong harness or cross-core timestamp agreement.

The clean revision passed CI run `36814828867` and the native Release suite (168/168). The
probe's executable matched a fresh Clang 21.1.8 `-std=c++23 -O3 -Wall -Wextra -Werror` build
byte for byte. CPU 1 had 11 full-busy two-second windows at 2400 MHz, 59-62 C package
temperature and 5.14-5.18 W, with no worker device IRQ, SMI, or thermal-throttle delta. The
saved frequency policy was restored byte for byte. An initial analysis assertion incorrectly
required more than one TSC cycle per nanosecond; it rejected valid raw data. The assertion was
corrected to a broad positive sanity range and the same retained data passed all host checks.

## Observations

| Reader | Adjacent delta median | p95 | Amortized read |
| --- | ---: | ---: | ---: |
| `steady_clock` | 28 ns | 30 ns | 27.99 ns |
| `CLOCK_MONOTONIC_RAW` | 28 ns | 31 ns | 28.35 ns |
| Fenced `RDTSC` | 28 cycles, about 34.7 ns | 30 cycles | 35.33 ns |
| Fenced `RDTSCP` | 33 cycles, about 40.9 ns | 34 cycles | 40.98 ns |

The seven TSC ratios spanned 0.806373382-0.806373417 cycles/ns, a 0.0000043% span. Both
POSIX clocks advertised 1 ns resolution, which is not their read cost or proven accuracy. On
this N150 and compiler, the serialized TSC variants were slower to read than `steady_clock`.
The existing RTT timer takes two clock reads per exchange, so its read floor is material for
roughly 400-700 ns RTT values. Do not subtract two isolated-read medians from a distribution:
call placement, fences, loop work, and tail behavior can differ. Throughput reads only at phase
boundaries; this probe does not explain its observed large between-row rate changes. Retain
`steady_clock` for now; changing the reader needs an end-to-end paired RTT comparison and a
portable fallback contract.

All 800,000 raw pairs, source and binary hashes, seven-ratio output, sidecar, host snapshots,
`turbostat`, the initial assertion failure record, corrected assessment, and save/restore records are
retained in ignored `results/clock-calibration-20261001/`.
