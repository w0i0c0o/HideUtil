# Git Workflow (GIT_WORKFLOW)

This repository hosts the one-click hide / restore desktop tool **HideUtil** (C++20 + native Win32).
This document fixes the version management rules; **every update must follow them**. Current baseline: the `main` branch, tag `v1.0.0`.

---

## 1. Branching Model

- **`main`**: the stable, releasable baseline branch. Day-to-day development is not committed here directly.
- Every change branches off `main` into a **short-lived branch**:
  - Feature development: `feature/<short-description>` (example: `feature/bring-to-front`)
  - Bug fix: `fix/<issue-description>` (example: `fix/quit-dialog-false-positive`)
- Use lowercase letters and hyphens in names; avoid spaces and non-ASCII characters.

## 2. Standard Change Flow

```bash
git checkout main
git pull --ff-only            # Sync with the remote (if one is configured)
git checkout -b fix/xxx       # Branch off main
# …edit code…
# Local regression verification (see section 4)…
git add -A
git commit -m "fix: false-positive quit dialog …"
git checkout main
git merge --no-ff fix/xxx     # Keep the branch history node for traceability
git branch -d fix/xxx         # Delete the short-lived branch after merging
```

## 3. Merge Rules

- Regression (section 4) **must** pass before merging back into `main`.
- Use `git merge --no-ff` to keep the feature-branch history node; if you prefer `main` to stay strictly linear, `git merge --squash` is also acceptable.
- Skipping commit hooks with `--no-verify` is forbidden (general discipline, even though this repository has no hooks configured at the moment).

## 4. Regression Verification (mandatory before merging)

1. **Rebuild**: `bash scripts/build.sh Release`
   The script injects the MSVC / Windows SDK / CMake environment automatically (no need to set PATH manually); the target is **28/28, zero errors and zero warnings**.
2. **Unit tests**: run `build/hideutil_tests.exe`; it must report `failures=0`.
3. **Standalone QA reachability test**: run `build/hideutil_qa_tests.exe`; everything must pass.
4. **(Optional, for hang-type defects) suspend regression**: rebuild and run the QA suspend cases under `build_qa/`
   (`bash build_qa/build_qa.sh`) to verify that hide/restore does not hang and that a forced kill can self-heal via `hideutil_cli.exe --restore`.

> Note: the GUI foreground watchdog in this sandbox may SIGTERM `hideutil_tests.exe`. In that case, use "clean build with zero warnings + line-by-line code review + reverse verification of the key cases" as supplementary evidence, and state it in the commit message.

## 5. Releases and Tagging

- Tag every external version on `main` with a **`vX.Y.Z`** semantic tag (example: `v1.1.0`):

  ```bash
  git tag -a v1.1.0 -m "v1.1.0 - add bring-to-front-on-restore switch"
  ```

- Only tag stable commits on `main`; never tag releases on feature branches.
- If a remote is configured, the tag needs `git push origin vX.Y.Z`.

## 6. Commit Message Convention (Conventional Commits)

| Prefix      | Meaning                                   |
|-------------|-------------------------------------------|
| `feat:`     | New feature                               |
| `fix:`      | Bug fix                                   |
| `chore:`    | Non-functional change: build / config / docs |
| `docs:`     | Documentation                             |
| `refactor:` | Refactoring (no behavior change)          |
| `test:`     | Tests                                     |

Example: `fix: "could not restore" prompt shown even when nothing was hidden (sticky flag not reset)`

## 7. Repository Content Boundaries (.gitignore)

**Tracked**: `src/`, `docs/`, `overview.md`, `overview.en.md`, `tests/`, `resources/`, `scripts/`, `CMakeLists.txt`, `app.manifest`, `build_qa/` (source and `build_qa.sh` only).

**Ignored**: `build/`, `*.exe`, `build_qa/bin/`, `*.log`, `build_log.txt`, `test_out.txt`, `.workbuddy/`, `.vs/`, etc.
That is: the deliverable `HideUtil.exe` and all build artifacts are not committed; source and scripts are version-controlled.

## 8. Commit Identity

This repository uses a **repository-level** identity (not a global one):

```bash
git config user.name  "Xing Wei"
git config user.email "wxing@ncu.edu.cn"
```

If that does not match your identity, change it locally (do not commit a global config change).

## 9. Git Hooks (Automatic Regression Before Every Commit)

The repository ships `hooks/pre-commit`, which automatically runs the three gates "rebuild + unit tests + standalone QA tests" before every commit and **aborts the commit if any step fails**. The script injects the MSVC / Windows SDK / CMake environment itself via `scripts/build.sh`, so no PATH setup is required beforehand.

### Enabling (once per clone)

The hook file is versioned with the repository, but git does not read in-repository hooks by default, so you must point the repository-level `core.hooksPath` at `hooks/`:

```bash
git config core.hooksPath hooks
```

After that, every subsequent `git commit` automatically triggers `hooks/pre-commit`.

### Hook Behavior

1. `bash scripts/build.sh Release` — target: 28/28, zero errors and zero warnings;
2. `build/hideutil_tests.exe` — unit tests, must exit 0 (`failures=0`);
3. `build/hideutil_qa_tests.exe` — standalone QA reachability test, must exit 0.

If any step returns non-zero, `pre-commit` does `exit 1` and the commit is rejected (the commit message you wrote is preserved in `.git/COMMIT_EDITMSG`; fix the problem and re-run `git commit`).

### Skipping the Hook (special cases only)

- For emergency commits, or when CI already runs the full regression, you can bypass it temporarily with `git commit --no-verify`; **`--no-verify` is forbidden in normal development**.
- If a full rebuild on every commit feels too slow, copy `hooks/pre-commit` to `hooks/pre-push` (run only before pushing), or change step 1 to the incremental `cmake --build build`.

### Remote CI (optional, once a remote is configured)

This repository is currently local-only. `core.hooksPath` only covers this machine. If it is later pushed to GitHub or another remote, add `.github/workflows/ci.yml` to run the same "build + test" suite on a cloud runner so that PRs and pushes are gated too (note: the VS/MSVC version on cloud Windows runners may differ from the local 14.51, so the toolchain paths in `scripts/build.sh` must be kept in sync).
