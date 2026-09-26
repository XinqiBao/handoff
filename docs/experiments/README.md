# Experiment Records

Experiments begin with a question or hypothesis, not an unbounded collection of numbers. Each
mechanism result identifies itself as a mechanism-isolation experiment or an implementation
comparison. Host calibration may instead be recorded as measurement-method characterization. All
follow the repository [benchmark methodology](../benchmark-methodology.md).

Use [`template.md`](template.md) for a new experiment. Keep setup and results specific to the
question, preserve individual trials where practical, and separate observations from explanations.
Do not commit routine smoke timings as experimental evidence.
Create a planned record only when a question has a defensible protocol and is promoted for
execution. Earlier unmeasured plans remain in Git history, not in the active experiment index.

## Completed experiments

- [Basic versus cache-line-separated SPSC](003-cache-line-spsc-comparison.md)
- [Basic versus cached-index SPSC](004-cached-index-spsc-comparison.md)
- [Basic scalar versus all-or-nothing batch SPSC](005-batch-spsc-comparison.md)
- [Bulk versus staged direct-slot SPSC](007-staged-spsc-comparison.md)
- [Head/tail SPSC versus sequence publication](008-sequence-publication-comparison.md)
- [Cost of reliable sequence fan-out](009-sequence-fan-out-comparison.md)
- [Cost of a fixed sequence dependency](010-sequence-dependency-comparison.md)
- [Lossy sequence-payload behavior under offered load](014-sequence-payload-offered-load.md)
- [Serialized ownership versus ordered MPSC publication](015-mpsc-ordered-publication.md)
- [Linux measurement host baseline](linux-host-baseline.md)
