# Experiment Records

Experiments begin with a question or hypothesis, not an unbounded collection of numbers. Each record
identifies itself as a mechanism-isolation experiment or an implementation comparison and follows
the repository [benchmark methodology](../benchmark-methodology.md).

Use [`template.md`](template.md) for a new experiment. Keep setup and results specific to the
question, preserve individual trials where practical, and separate observations from explanations.
Do not commit routine smoke timings as experimental evidence.

## Planned experiments

- [Basic SPSC throughput across payload and capacity](001-basic-spsc-throughput.md)
- [Basic SPSC ping-pong RTT across payload and capacity](002-basic-spsc-ping-pong.md)
- [Basic versus cache-line-separated SPSC](003-cache-line-spsc-comparison.md)
- [Basic versus cached-index SPSC](004-cached-index-spsc-comparison.md)
- [Basic scalar versus all-or-nothing batch SPSC](005-batch-spsc-comparison.md)
- [Fixed bulk versus best-effort burst SPSC](006-bulk-burst-spsc-comparison.md)
- [Bulk versus staged direct-slot SPSC](007-staged-spsc-comparison.md)
- [Head/tail SPSC versus sequence publication](008-sequence-publication-comparison.md)
- [Cost of reliable sequence fan-out](009-sequence-fan-out-comparison.md)
- [Cost of a fixed sequence dependency](010-sequence-dependency-comparison.md)
- [Generic payload versus fixed record slots](011-fixed-record-comparison.md)
- [Fixed slots versus a variable-record byte ring](012-variable-record-comparison.md)
