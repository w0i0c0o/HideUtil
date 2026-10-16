# Contributing Guide (CONTRIBUTING)

Welcome to the development of **HideUtil** (one-click desktop hide / restore tool).

Before you start, please read [GIT_WORKFLOW.en.md](./GIT_WORKFLOW.en.md); all changes must follow its branching, merge and regression rules.

Quick summary:

- Cut a `feature/xxx` or `fix/xxx` branch from `main`; **pushing directly to `main` is forbidden**.
- Before merging back into `main`, the regression suite must pass: rebuild (`bash scripts/build.sh Release`) + unit tests (`build/hideutil_tests.exe`) + standalone QA (`build/hideutil_qa_tests.exe`).
- Tag every released version on `main` as `vX.Y.Z`.
- Commit messages use Conventional Commits prefixes (`feat:` / `fix:` / `chore:` / `docs:` …).
- The deliverable `HideUtil.exe` and all build artifacts are not committed; source and scripts are version-controlled (see `.gitignore`).
