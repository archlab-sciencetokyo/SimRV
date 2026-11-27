# Contributing

## What to Propose

Issues are the place for bug reports, questions, general feedback, and feature or design
proposals. For substantial work, discuss the goal and approach in an issue before writing code.
This is especially helpful for changes to CPU or device behavior, public interfaces, configuration
formats, or project direction.

Pull requests are welcome for plugins and plugin behavior, as well as focused fixes, documentation,
tests, and other changes that fit SimRV's goals. You do not need prior approval for a small,
straightforward correction. For larger changes, link the issue so the proposed implementation has
shared context before review.

Opening a pull request does not guarantee that the change will be merged. Maintainers review
changes for project fit, correctness, compatibility, scope, and validation. If a proposal is not a
fit for the core project, maintainers may suggest keeping it as an external plugin or separate
project.

## License and Attribution

SimRV is distributed under the MIT License. Only contribute work you have the right to share under
that license. If you include or adapt third-party code, identify its source and license, preserve
required notices, and update `THIRD_PARTY_NOTICES.md` when needed. Mention new dependencies in the
pull request.

If a change affects research claims, published results, or citation details, update the relevant
documentation and `CITATION.cff` when appropriate. Use the exact release tag when citing SimRV.

## Branching Model

This repository uses a lightweight integration flow:

- main: stable branch, intended to stay releasable.
- dev: integration branch for day-to-day development.
- `feature/*`, `fix/*`, and `release/*`: short-lived branches created from `dev`.

All changes reach protected branches through pull requests. Do not push directly to `dev` or
`main`; require passing checks and review, then delete the source branch after merge. Use
`release/<version>` (e.g., `release/3.0.0-alpha.3`) for coordinated release qualification and PR delivery
to `dev`. Do not push tags or publish releases until all qualification matrix checks are green.

## Typical Workflow

1. Sync and branch from dev:

```bash
git fetch origin
git checkout dev
git pull
git checkout -b feature/<topic>
```

1. Commit small logical changes and keep your branch current. Rebase a private branch, or merge
   `origin/dev` when the branch is already shared:

```bash
git fetch origin
git rebase origin/dev
# Shared branch alternative: git merge --no-ff origin/dev
```

1. Open a pull request into dev.

2. After review and required CI pass, squash-merge or rebase-merge into `dev` and delete the
   short-lived branch.

3. For releases, open a pull request from dev into main.

## Commit Guidance

- Keep commit messages short and imperative.
- Prefer one concern per commit.
- Include build/test context in PR descriptions.
- Add semantic tests for new behavior and update the support/compliance matrix.
- State whether a change affects reproducibility, experiment outputs, CLI behavior, or schemas.
- Treat new skips, sanitizer findings, undocumented behavior changes, and unsupported headline
  claims as blockers.

## Validation Guidance

Before opening a PR, run at least:

```bash
# RV64 Build and validation
cmake --preset rv64-release
cmake --build --preset rv64-release
cmake --build --preset rv64-release --target integration-gate

# RV32 Build and validation
cmake --preset rv32-release
cmake --build --preset rv32-release
cmake --build --preset rv32-release --target integration-gate
```

During the final 3.0 candidate window, CLI names, automation output, schemas, and asset names are
frozen. Changes to CPU semantics, MMU/CSR behavior, devices, concurrency, test infrastructure, or
experiment results restart the two-clean-run stabilization requirement.
