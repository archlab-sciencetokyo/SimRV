# Contributing

## Branching Model

This repository uses a lightweight integration flow:

- main: stable branch, intended to stay releasable.
- dev: integration branch for day-to-day development.
- feature/* and fix/*: short-lived branches created from dev.

## Typical Workflow

1. Sync and branch from dev:

```bash
git fetch origin
git checkout dev
git pull
git checkout -b feature/<topic>
```

2. Commit small logical changes and keep your branch current:

```bash
git fetch origin
git rebase origin/dev
```

3. Open a pull request into dev.

4. After review and CI pass, merge to dev.

5. For releases, open a pull request from dev into main.

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
cmake --build --preset rv64-release --target isa-gate

# RV32 Build and validation
cmake --preset rv32-release
cmake --build --preset rv32-release
cmake --build --preset rv32-release --target isa-gate
```

During the final 2.0 candidate window, CLI names, automation output, schemas, and asset names are
frozen. Changes to CPU semantics, MMU/CSR behavior, devices, concurrency, test infrastructure, or
experiment results restart the two-clean-run stabilization requirement.
