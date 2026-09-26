# Repository Guidance

Follow applicable user-level instructions first. If `.context/README.md` exists, read it next and
follow its local read path. Then read `README.md` and `docs/roadmap.md` before changing the
repository.

Read additional documents according to the task instead of loading the entire repository context:

- mechanism design or implementation: `docs/architecture.md`, `docs/design-space.md`,
  `docs/testing-strategy.md`, and the relevant mechanism and inspiration notes;
- benchmark or experiment work: `docs/benchmark-methodology.md`, `docs/reproducibility.md`, and the
  relevant experiment record;
- build, tooling, or CI work: `docs/reproducibility.md` and the affected build files;
- roadmap or project-policy work: all stable project documents needed to check consistency.

Tracked repository documents are the canonical source for architecture, semantics, methodology,
research direction, and measured evidence. The ignored `.context/` directory may hold bounded work
notes; it must not override tracked project truth. Do not commit prompts, session transcripts,
routine reports, or `.context/` contents. If local status disagrees with Git, CI, or tracked
documents, trust the inspected evidence and tracked documents, then correct the local status.

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

## Research package execution

A coherent question or change is the unit of planning, validation, and commit history; it need not
be a conversation boundary. Before starting, inspect the branch, worktree, recent history, relevant
CI state, routed documents, and the current research direction. State the question, non-goals,
semantics, and validation criteria. Split a large package into understandable boundaries when its
semantics are clear. Do not leave several mechanisms half-implemented to extend a session.

After each package, run checks appropriate to its scope: builds, tests, sanitizers, formatting,
static analysis, and benchmark smoke commands where relevant. Review the complete diff and Git
status for scope, readability, and documentation accuracy. Keep mechanism notes and completed
experiment records current; update the research direction when findings change it. Make coherent
Conventional Commits, push, and wait for required CI checks. Update bounded local `.context/`
status only when present and useful. Continue with another eligible package when its question and
validation are clear; stop for a material semantic choice, external measurement hardware, red CI
after reasonable repair, or exhausted requested scope.
