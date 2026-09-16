# 贡献指南（CONTRIBUTING）

欢迎参与 **HideUtil**（一键隐藏 / 恢复桌面工具）的开发。

开始之前请先阅读 [GIT_WORKFLOW.md](./GIT_WORKFLOW.md)，所有改动必须遵循其中的分支、合并与回归规则。

要点速览：

- 从 `main` 切出 `feature/xxx` 或 `fix/xxx` 分支开发，**禁止直接推 `main`**。
- 合回 `main` 前必须跑通回归：重建（`bash scripts/build.sh Release`）+ 单元测试（`build/hideutil_tests.exe`）+ 独立 QA（`build/hideutil_qa_tests.exe`）。
- 每个发布版本在 `main` 上打 `vX.Y.Z` 标签。
- 提交信息采用 Conventional Commits 前缀（`feat:` / `fix:` / `chore:` / `docs:` …）。
- 交付物 `HideUtil.exe` 与编译产物不入库，源码与脚本受控（详见 `.gitignore`）。
