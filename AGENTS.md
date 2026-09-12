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
and roadmap state. The ignored `.context/` directory may hold current local status or bounded work
notes; it must not override tracked project truth. Do not commit prompts, session transcripts,
routine reports, or `.context/` contents. If local status disagrees with Git, CI, or the tracked
roadmap, trust the inspected evidence and tracked documents, then correct the local status.

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

## Stage execution

The roadmap stage is the unit of planning, validation, and commit history; it is not necessarily a
conversation boundary. A long-running session may complete consecutive eligible stages without
asking for a new prompt after each one.

Before each stage:

1. Inspect the branch, worktree, recent history, and relevant CI state.
2. Reread the routed documents and the current roadmap entry.
3. Define the stage goal, non-goals, and validation criteria.

After each stage:

1. Run the relevant builds, tests, sanitizers, formatting, static analysis, and smoke commands.
2. Review the complete diff and `git status` for scope, readability, and documentation accuracy.
3. Mark only completed work in `docs/roadmap.md`; identify exactly one next executable stage and
   replace its detailed `Next stage` contract when work remains. Do not accumulate completed stage
   packets in the roadmap.
4. Create coherent Conventional Commit(s) with a visible stage boundary, push them, and wait for
   the required CI checks.
5. Update local `.context/` status when present, then continue to the next eligible roadmap stage.

Stop at a clean stage boundary only when the requested roadmap scope is exhausted, a semantic
choice materially needs user input, external measurement hardware is required, or CI remains red
after reasonable repair attempts. If a future stage is too large but its semantics are clear, split
it into smaller roadmap stages rather than stopping or implementing it as one opaque change. Do not
leave several mechanisms half-implemented merely to continue the session.
