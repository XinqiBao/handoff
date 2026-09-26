# Experiment: Linux measurement host baseline

- Type: measurement-method characterization
- Status: complete
- Measurement revision: `273c757109e9b1650ef94938d6086e32dad495bd`
- Date: 2026-09-14

## Question and setup

Was the stock physical four-core Intel N150 host repeatable enough for bounded relative mechanism
comparisons after warm-state conditioning? The exact revision passed Linux Release, format,
clang-tidy, ASan/UBSan, and TSan gates. A fresh native Release build ran basic throughput with a
64-byte payload, 1024 slots, 20,000,000 measured messages, 2,000,000 warmup messages, and seven
retained trials. The process was restricted to coordinator CPU 0; producer CPU 1 and consumer CPU 2
were on separate same-node physical cores with effective masks verified. A later fixed three-worker
placement used consumer CPU 3, whose historical network softirq activity was higher.

## Results and interpretation

The first group's median was 8.062 million messages/s, sample CV 0.937%, and full range 2.305%.
Its first-to-last change was -1.129% as the package warmed. After a 40,000,000-message conditioning
run, the retained warm-state group had a median of 8.068 million messages/s, sample CV 0.280%, full
range 0.915%, first-to-last change +0.269%, and fitted slope +0.025% of the median per trial. Every
timed trial exceeded 2.4 seconds. Turbostat observed roughly 3.4-3.6 GHz busy frequency and 70-78 C
in the warm group; hardware thermal-throttle counters did not change.

The host ran its stock `intel_pstate` `powersave` governor with `balance_performance` EPP. No
governor, EPP, perf policy, IRQ, kernel, or boot setting was changed. This supports bounded relative
comparisons after consistent warm-state conditioning. It is not a mechanism-performance claim;
effects near the observed dispersion require caution. Later experiments retain their own setup and
limits. Raw CSV, sidecars, affinity checks, turbostat output, and analysis remain in ignored local
`results/l1/l1a-baseline/` storage on both machines.
