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
