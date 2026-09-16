# Git 工作流（GIT_WORKFLOW）

本仓库用于「一键隐藏 / 恢复」桌面工具 **HideUtil**（C++20 + Win32 原生）。
本文档固化版本管理规则，**所有更新必须遵循**。当前基线：`main` 分支、标签 `v1.0.0`。

---

## 1. 分支模型

- **`main`**：稳定、可发布的基线分支。日常开发不直接提交到这里。
- 任何改动都从 `main` 切出**短生命周期分支**：
  - 功能开发：`feature/<简短描述>`（例：`feature/bring-to-front`）
  - 缺陷修复：`fix/<问题描述>`（例：`fix/quit-dialog-false-positive`）
- 命名用小写 + 连字符，避免空格与中文。

## 2. 标准改动流程

```bash
git checkout main
git pull --ff-only            # 与远端同步（若已配置远端）
git checkout -b fix/xxx       # 从 main 切分支
# …编辑代码…
# 本地回归验证（见第 4 节）…
git add -A
git commit -m "fix: 修复退出误报……"
git checkout main
git merge --no-ff fix/xxx     # 保留分支痕迹，便于追溯
git branch -d fix/xxx         # 合并后删除短生命周期分支
```

## 3. 合并规则

- 合回 `main` **前必须**通过回归（第 4 节）。
- 使用 `git merge --no-ff` 保留特性分支历史节点；若希望 `main` 保持严格线性，也可用 `git merge --squash`。
- 禁止用 `--no-verify` 跳过提交钩子（通用纪律，即便本仓库暂未配置钩子）。

## 4. 回归验证（合并前必做）

1. **重建**：`bash scripts/build.sh Release`
   脚本会自动注入 MSVC / Windows SDK / CMake 环境（无需手动设置 PATH），目标 **28/28 零错误零警告**。
2. **单元测试**：运行 `build/hideutil_tests.exe`，要求 `failures=0`。
3. **独立 QA 可达性测试**：运行 `build/hideutil_qa_tests.exe`，要求全过。
4. **（可选，针对卡死类缺陷）挂起回归**：重建并运行 `build_qa/` 下的 QA 挂起用例
   （`bash build_qa/build_qa.sh`），验证 Hide/Restore 不卡死、强杀后可经 `hideutil_cli.exe --restore` 自愈。

> 注：本沙箱 GUI 前台 watchdog 可能 SIGTERM `hideutil_tests.exe`，此时以「构建零警告 + 逐行代码核对 + 关键用例反向验证」作为补充证据，并在提交说明中注明。

## 5. 发布与打标签

- 每个对外版本在 `main` 上打 **`vX.Y.Z`** 语义化标签（例：`v1.1.0`）：

  ```bash
  git tag -a v1.1.0 -m "v1.1.0 - 新增恢复置顶开关"
  ```

- 仅对 `main` 上的稳定提交打发布标签，不在特性分支打发布标签。
- 若已配置远端，标签需 `git push origin vX.Y.Z`。

## 6. 提交信息约定（Conventional Commits）

| 前缀        | 含义                         |
|-------------|------------------------------|
| `feat:`     | 新功能                       |
| `fix:`      | 缺陷修复                     |
| `chore:`    | 构建 / 配置 / 文档等非功能改动 |
| `docs:`     | 文档                         |
| `refactor:` | 重构（行为不变）             |
| `test:`     | 测试                         |

示例：`fix: 退出时未隐藏却弹「未能恢复」提醒（粘滞标志复位）`

## 7. 仓库内容边界（.gitignore）

**入库**：`src/`、`docs/`、`overview.md`、`tests/`、`resources/`、`scripts/`、`CMakeLists.txt`、`app.manifest`、`build_qa/`（仅源码与 `build_qa.sh`）。

**忽略**：`build/`、`*.exe`、`build_qa/bin/`、`*.log`、`build_log.txt`、`test_out.txt`、`.workbuddy/`、`.vs/` 等。
即：交付物 `HideUtil.exe` 与所有编译产物不入库，源码与脚本受控。

## 8. 提交身份

本仓库使用**仓库级**身份（非全局）：

```bash
git config user.name  "Xing Wei"
git config user.email "wxing@ncu.edu.cn"
```

若与你的身份不符，请本地修改（不要提交全局配置改动）。

## 9. Git 钩子（提交前自动回归）

仓库内置 `hooks/pre-commit`，在每次提交前自动执行「重建 + 单元测试 + 独立 QA 测试」三道门禁，**任一步失败即中止提交**。脚本通过 `scripts/build.sh` 自行注入 MSVC / Windows SDK / CMake 环境，无需预先配置 PATH。

### 启用（每个克隆只需一次）

钩子文件随仓库版本化，但 git 默认不读取仓库内钩子，需把仓库级 `core.hooksPath` 指向 `hooks/`：

```bash
git config core.hooksPath hooks
```

配置后，后续所有 `git commit` 都会自动触发 `hooks/pre-commit`。

### 钩子行为

1. `bash scripts/build.sh Release` —— 目标 28/28 零错误零警告；
2. `build/hideutil_tests.exe` —— 单测，要求退出码 0（`failures=0`）；
3. `build/hideutil_qa_tests.exe` —— 独立 QA 可达性测试，要求退出码 0。

任一步返回非 0，`pre-commit` 即 `exit 1`，提交被拒绝（已写入的提交信息会保留在 `.git/COMMIT_EDITMSG`，修正后重跑 `git commit` 即可）。

### 跳过钩子（仅限特殊场景）

- 紧急提交、或 CI 环境已另跑完整回归时，可用 `git commit --no-verify` 临时绕过；**正常开发禁止 `--no-verify`**。
- 若觉得每次提交都全量重建太慢，可将 `hooks/pre-commit` 复制为 `hooks/pre-push`（仅推送前跑），或把第 1 步改为增量 `cmake --build build`。

### 远端 CI（可选，待配置远端后）

本仓库当前为本地仓库。`core.hooksPath` 只覆盖本机。若之后挂到 GitHub 等远端，应另加 `.github/workflows/ci.yml` 在云端 runner 上跑同一套「构建 + 测试」，使 PR/推送也受门禁约束（注意：云端 Windows runner 的 VS/MSVC 版本可能与本机 14.51 不同，需同步 `scripts/build.sh` 中的工具链路径）。

