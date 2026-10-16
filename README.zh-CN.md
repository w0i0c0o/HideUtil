# HideUtil

**Windows 一键隐藏 / 恢复工具（Boss Key）。**

---

## 下载 —— 你只需要一个文件

> ### [`HideUtil.exe`](../../releases/latest/download/HideUtil.exe)
>
> **这就是全部。** 本仓库里都是源码和文档；要**使用**这个工具，你只需要下载**一个可执行文件**，
> 别的什么都不用。无需安装程序、无需 Visual C++ 运行库、无需运行时、无需附带 DLL、无需复制配置文件。

**三步开始：**

1. 从[最新版本](../../releases/latest)下载 [`HideUtil.exe`](../../releases/latest/download/HideUtil.exe)。
2. 双击运行并接受 UAC 提权提示——它需要管理员权限才能管理高完整性级别的窗口。
   **什么都不会出现：没有窗口、没有托盘图标、没有控制台。** 这是刻意的。
3. 按 `Ctrl+Alt+F12` 隐藏你配置好的应用，按 `Ctrl+Alt+F11` 打开设置界面。

每个版本都附带可执行文件的 **SHA-256**，建议运行前核对一下。
[查看所有版本 →](../../releases/latest)

> 还不确定这是不是你要的？先读[能力范围与限制](#能力范围与限制)。简而言之：HideUtil 隐藏的是
> 运行中应用的**可见痕迹**，它刻意不提供在任务管理器中隐藏进程的能力。

---

## 它做什么

HideUtil 是一个 Windows 用户态桌面工具。一个全局热键即可隐藏指定应用的**全部可见痕迹**——
窗口、任务栏按钮、Alt+Tab 条目、托盘图标与音频；再按一次，窗口按原位置、原大小、原最大化状态
精确回归。

**全程零驱动、零内核 hook，只使用受支持的公开用户态 API。**

---

## 功能

| 方面 | 能力 |
|---|---|
| 窗口 | 隐藏顶层窗口与任务栏按钮（`ShowWindow` + `ITaskbarList::DeleteTab`） |
| Alt+Tab | 通过 `WS_EX_TOOLWINDOW` 移除条目 |
| 自显示看门狗 | `SetWinEventHook(EVENT_OBJECT_SHOW)` 捕获应用自己弹出的窗口并再次隐藏 |
| 音频 | WASAPI 会话级静音，按应用生效 |
| 位置保真 | 快照 `WINDOWPLACEMENT` 与窗口样式，还原时几何与最大化态一并恢复 |
| 进程 | 可选 `NtSuspendProcess` 冻结（从 `ntdll` 动态解析） |
| 托盘图标 | 隐藏自身图标；Win10 经典托盘中他人图标按尽力而为处理 |
| 自身痕迹 | 无窗口、无托盘图标、无控制台 |
| 热键 | 原生设置界面，支持录制式热键捕获与冲突检测 |
| 规则 | 多应用规则化配置（exe 路径 / 进程名 / 窗口类名） |
| 界面语言 | 简体中文与英文，默认跟随系统 UI 语言 |

恢复流程是**非阻塞**的：所有跨线程窗口操作都以 `IsWindowResponsive` 探测
（`SendMessageTimeoutW(WM_NULL, SMTO_ABORTIFHUNG)`）为门禁，因此目标窗口停止泵消息时，
绝不会把 HideUtil 拖死。

---

## 能力范围与限制

本项目如实声明做不到的事。这些是设计取舍，不是缺陷。

**已实现可用**：窗口 / 任务栏 / Alt+Tab 隐藏、音频静音、窗口位置保真、进程挂起、热键重映射。

**有意降级**（不做 hack）：

- **Win11 下他人应用的托盘图标** —— 通知区已改为 XAML 渲染，没有受支持的 API；此时返回
  `false`，而不是去篡改系统。
- **独占模式 / ASIO 音频** —— 绕过会话音量控制，静音对其无效。
- **防截屏** —— `SetWindowDisplayAffinity` 只对调用进程自身的窗口生效。
- **虚拟桌面** —— 文档化 API 无法创建桌面，单桌面环境下返回 `false`。

**不做，也永远不会做**（只解释原因，不提供绕过）：

- **在任务管理器中隐藏进程** —— 用户态没有受支持 API，任何实现都要落到内核驱动并绕过
  DSE/PatchGuard/HVCI，即 rootkit 行为。
- **"隐藏"热键占用** —— 伪需求；Windows 无法枚举他人应用的热键。
- **清除持久化取证痕迹** —— 需篡改系统，且清除行为本身就是恶意特征。本项目的策略是
  "尽量不产生痕迹"。
- **隐藏开机自启项** —— 与"自启"本身的性质矛盾。

> **关于关机**：被隐藏的应用**仍会**出现在 Windows 的「正在阻止关机的应用」列表中。该列表是
> 系统根据各应用对 `WM_QUERYENDSESSION` 的响应生成的，由目标应用自己决定；隐藏可见痕迹不会
> 改变它。

---

## 环境要求

- Windows 10 / 11 x64（用户态；主程序请求管理员权限以跨越 UIPI）
- Visual Studio Build Tools 与 MSVC 14.5x
- Windows SDK 10.0.26100.0（或兼容版本）
- CMake 3.20+ 与 Ninja

## 构建

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

> **构建注意**：**链接**步骤请在原生 shell（PowerShell 或 `cmd`）中完成。在 Git Bash 下 `TMP`
> 是 POSIX 路径（`/tmp`），`link.exe` 无法解析，链接会报
> `LNK1104: cannot open file 'C:\WINDOWS\lnk{...}.tmp'`。编译用 Git Bash 没问题，**只有链接受影响**。

产出 4 个目标：

| 目标 | 用途 |
|---|---|
| `build/HideUtil.exe` | **正式交付物**。无窗口、无托盘图标、无控制台 |
| `build/hideutil_cli.exe` | 命令行外壳，用于自动化与诊断 |
| `build/hideutil_tests.exe` | 单元测试 |
| `build/hideutil_qa_tests.exe` | 设置界面可达性测试 |

### 跑测试

```powershell
build\hideutil_tests.exe      # checks=466 failures=0 -> PASS
build\hideutil_qa_tests.exe   # checks=6   failures=0 -> PASS
```

> `checks=` **不是定值**：`TestRestoreDoesNotStealForeground` 依赖系统是否把前台授予测试进程，
> 因此总数为「前台可用 466 / 前台锁定 461」。两者均属 PASS。

---

## 用法

### 热键（默认）

| 热键 | 动作 |
|---|---|
| `Ctrl+Alt+F12` | 隐藏 / 恢复切换 |
| `Ctrl+Alt+F11` | 打开设置界面 |

设置界面还可以这样唤起：二次启动 `HideUtil.exe`（单实例互斥量唤醒已运行实例），或
`HideUtil.exe --settings`。

### 命令行

```powershell
hideutil_cli.exe --list                 # 列出可见顶层窗口
hideutil_cli.exe --targets              # 列出候选目标应用
hideutil_cli.exe --hide notepad.exe     # 隐藏
hideutil_cli.exe --restore              # 全部恢复
hideutil_cli.exe --status               # 当前状态
hideutil_cli.exe --settings-dump        # 导出生效配置
```

CLI 诊断输出**刻意保持英文**，不随界面语言变化，这样日志与脚本解析不会因语言环境而漂移。

---

## 配置

配置位于 `%APPDATA%\HideUtil\config.json`，首次运行自动生成。默认不写注册表，也不注册开机自启。

```jsonc
{
  "version": 1,
  "language": "auto",          // "auto" | "en-US" | "zh-CN"
  "hotkey": {
    "hideToggle":   { "modifiers": ["CTRL", "ALT"], "key": "F12" },
    "openSettings": { "modifiers": ["CTRL", "ALT"], "key": "F11" }
  },
  "restore": { "bringToFront": false },
  "rules": [
    {
      "id": "rule-1",
      "enabled": true,
      "name": "EXCEL.EXE",
      "match": {
        "exePath": "C:\\...\\EXCEL.EXE",
        "processName": "EXCEL.EXE",
        "windowClass": "XLMAIN",
        "matchMode": "all"           // "any" | "all"
      },
      "actions": {
        "hideWindow": true,
        "hideTrayIcon": true,
        "muteAudio": true,
        "suspendProcess": false,
        "moveToVirtualDesktop": false,
        "hideFromAltTab": true,
        "clearJumpList": false
      }
    }
  ]
}
```

完整字段说明见 [`docs/02-构建与运行说明.md`](docs/02-构建与运行说明.md) 第 5 节。

**修改 `language` 后必须重启 HideUtil 才生效** —— 本地化只在启动时初始化一次，不支持热重载：

```powershell
taskkill /F /IM HideUtil.exe
```

---

## 可移植性

`build/HideUtil.exe` 是**单文件可移植**的。CRT 静态链接（`/MT`），应用图标与语言文档以资源
内嵌（`ICON` / `RCDATA`），`config.json` 与 `state.json` 在需要时于 `%APPDATA%\HideUtil\` 下
自动生成。

`dumpbin /dependents` 只列出系统 DLL，因此**不需要安装 Visual C++ 运行库**：

```
USER32.dll  ole32.dll  COMCTL32.dll  SHELL32.dll
ADVAPI32.dll  dwmapi.dll  KERNEL32.dll  GDI32.dll
```

迁移到另一台 64 位 Windows 机器时，**只复制 `HideUtil.exe` 这一个文件**即可（若要同时迁移规则，
再拷一份 `config.json`）。程序未签名，首次运行可能触发 SmartScreen 提示。

---

## 文档

| 文档 | 内容 |
|---|---|
| [`docs/01-架构设计与可行性分析.md`](docs/01-架构设计与可行性分析.md) | 架构设计与可行性分析 |
| [`docs/02-构建与运行说明.md`](docs/02-构建与运行说明.md) | 构建、运行、配置、限制 |
| [`docs/class-diagram.mermaid`](docs/class-diagram.mermaid)、[`docs/sequence-diagram.mermaid`](docs/sequence-diagram.mermaid) | 类图与时序图 |
| [`overview.md`](overview.md) | 交付总览 |
| [`CONTRIBUTING.md`](CONTRIBUTING.md)、[`GIT_WORKFLOW.md`](GIT_WORKFLOW.md) | 协作与分支规范 |

英文版本：[`docs/en/01-architecture-and-feasibility.md`](docs/en/01-architecture-and-feasibility.md)、
[`docs/en/02-build-and-run.md`](docs/en/02-build-and-run.md)、[`overview.en.md`](overview.en.md)、
[`CONTRIBUTING.en.md`](CONTRIBUTING.en.md)、[`GIT_WORKFLOW.en.md`](GIT_WORKFLOW.en.md)。

---

## 许可证

**GPL-3.0** —— 见 [`LICENSE`](LICENSE)。

简而言之：你可以自由使用、修改和分发本软件（包括商业用途），但任何衍生作品也必须以 GPL-3.0
发布，并提供源代码。
