# Repository Guidance

Before changing this repository, read:

1. `README.md`
2. `docs/architecture.md`
3. `docs/design-space.md`
4. `docs/benchmark-methodology.md`
5. `docs/reproducibility.md`
6. `docs/roadmap.md`
7. The mechanism or experiment note relevant to the current stage

Follow any applicable user-level context before this repository guidance.

## Engineering rules

- Keep mechanisms locally understandable. Prefer explicit code over policy hierarchies, abstract
  queue interfaces, runtime registries, and indirect control flow.
- Organize code by the mechanism being studied, not by an external project name.
- Preserve useful intermediate implementations when they represent distinct experiments.
- Put common invariants in shared tests and mechanism-specific invariants beside the mechanism.
- Do not benchmark a mechanism until its required correctness tests pass.
- Keep benchmark-side work equivalent across compared implementations.
- Describe direct storage access precisely; do not use `zero-copy` as a vague synonym.
- Treat LMAX Disruptor, Firedancer, and DPDK as inspirations unless compatibility is explicitly
  implemented and verified.
- Use C++23 and Clang. Keep portable behavior on Linux and macOS; isolate optional Linux features.
- Use Conventional Commits with short English subjects.

## Stage discipline

Before a major stage, inspect repository state, reread the relevant documents, define the goal and
non-goals, and state the validation criteria. After the stage:

1. Run the relevant build, tests, sanitizers, formatting, static analysis, and smoke commands.
2. Review the complete diff and `git status`.
3. Confirm documentation matches implemented behavior and no unrelated scope was added.
4. Create one coherent commit, push it, and update the roadmap only for completed work.

Long-running work should proceed through bounded stages. Stop at a clean boundary rather than
partially implementing several unrelated mechanisms.
