# 交付总览：一键隐藏 / 恢复工具（Boss Key）

> 交付日期：2026-09-11
> 交付团队：software-hideutil（软件开发团队 SOP · 架构评审 → 实现 → 测试验证）
> 工作区：`E:\幸玮代码\Buddy_Space\6_Fade`

## 一、TL;DR

一个 Windows 用户态桌面工具：全局热键一键隐藏/恢复指定应用的全部显示痕迹（窗口、任务栏按钮、Alt+Tab 条目、音频），支持多应用配置化管理，并提供**可修改全局快捷键的原生设置界面**。**全程零驱动、零内核 hook。**

- **构建状态**：零错误、零警告，产出 4 个可执行目标
- **测试状态**：单元测试 `checks=195 failures=0 -> PASS`；QA 自动化回归 `checks=6 failures=0 -> PASS`
- **已知缺陷**：0（累计发现并修复 5 项真实产品缺陷 + 1 项测试隔离缺陷 + 1 项 P0 卡死/不可恢复缺陷 + 1 项 P1 恢复位置/黑屏缺陷 + 1 项 P1 前台抢占缺陷，均有回归用例覆盖）

## 二、交付物

### 源码与构建

| 路径 | 说明 |
|---|---|
| `CMakeLists.txt` | 构建入口（Ninja + MSVC） |
| `scripts/build.sh` | 一键构建脚本（固化了本机所需的 MSVC 环境注入） |
| `app.manifest` | 主程序清单（`requireAdministrator`） |
| `resources/config.schema.json` | 配置 JSON Schema |
| `src/main.cpp` | `wWinMain` 入口（单实例 + `--settings`） |
| `src/self/` | 自身外壳：`SelfShell`（message-only window）/ `HotkeyManager`（热键注册与重注册回滚）/ `Logger`（默认关闭） |
| `src/core/` | 核心引擎：`Types` / `Json`（自实现，零依赖）/ `AppRuleStore` / `StateStore` / `ProcessWindowResolver` / `HideOrchestrator` / `WindowHider` / `AudioMuter` / `WinEventHook` / `TrayIconHider` / `ProcessController` / `ProcessWatcher` / `VirtualDesktopMover` / `JumpListCleaner` |
| `src/ui/SettingsWindow.cpp` | 设置界面（原生 Win32，热键录制 + 规则管理） |
| `src/cli/main_cli.cpp` | 命令行外壳（asInvoker，自动化验证抓手） |
| `tests/core_tests.cpp` | 纯逻辑单元测试（113 项断言） |
| `tests/qa_ui_tests.cpp` | QA 追加：设置界面可达性测试 |

### 文档

| 路径 | 说明 |
|---|---|
| `docs/01-架构设计与可行性分析.md` | 架构设计与可行性分析（13 章 / 865 行） |
| `docs/02-构建与运行说明.md` | 构建、运行、配置、限制说明（含快照语义约定 8.1 节） |
| `docs/class-diagram.mermaid` / `docs/sequence-diagram.mermaid` | 类图与时序图 |
| `overview.md` | 本文件 |

### 构建产物

| 目标 | 权限 | 用途 |
|---|---|---|
| `build/HideUtil.exe` | `requireAdministrator` | **正式交付物**：无窗口 / 无托盘 / 无控制台 |
| `build/hideutil_cli.exe` | `asInvoker` | 命令行测试外壳（`--list` / `--hide` / `--restore` / `--status` / `--suspend` / `--resume` / `--settings-dump`） |
| `build/hideutil_tests.exe` | `asInvoker` | 单元测试 |
| `build/hideutil_qa_tests.exe` | `asInvoker` | QA 追加：设置界面可达性测试 |

## 三、功能与实测证据

| 功能 | 实现方式 | 实测证据 |
|---|---|---|
| 隐藏窗口 + 任务栏按钮 | `ShowWindow(SW_HIDE)` + `ITaskbarList::DeleteTab` | 隐藏后窗口计数 1→0，恢复后 0→1 |
| 移出 Alt+Tab | `WS_EX_TOOLWINDOW` | 随窗口隐藏一并生效 |
| 兜住应用自我重显 | `SetWinEventHook(EVENT_OBJECT_SHOW)` | QA 强制 `ShowWindow(SW_SHOW)` 后再 hide → restore 仍可恢复，矩形不变 |
| **恢复时置顶（新增配置项）** | `AppConfig::bringRestoredToFront`（默认 `false`）+ 设置界面「恢复时将窗口置顶」复选框 | 勾选=恢复后 `SetForegroundWindow` 置顶；不勾选=恢复后把前台归还给用户恢复前正在使用的窗口（WPS 是顶层时不受影响） |
| **前台归还（P1 回归）** | `HideOrchestrator::RestoreAll` 恢复前记录 `GetForegroundWindow()`，所有窗口恢复完后**一次性**归还前台 | 修复"不勾选置顶但窗口仍被置顶"：`SetWindowPlacement` 无 no-activate 选项，恢复必然激活窗口 | 反向验证：临时禁用修复后 3 项断言失败（证明 bug 可复现且断言真实生效），启用后 195/195 全绿 |
| 静音应用音频 | WASAPI 会话级 `ISimpleAudioVolume::SetMute` + `IAudioSessionNotification` | 代码路径 + 编译验证（测试目标无音频会话，**未做真实听感验证**） |
| 隐藏自身痕迹 | `wWinMain` + message-only window，不注册托盘 | 无窗口 / 无托盘 / 无控制台 |
| 窗口位置保真 | 快照 `WINDOWPLACEMENT` + `GWL_STYLE`/`GWL_EXSTYLE` | 移到 (320,240,980,700) → 隐藏 → 恢复后矩形**完全一致**，HWND 不变 |
| 挂起进程（可选） | `ntdll!NtSuspendProcess`（`GetProcAddress` 动态解析） | CPU 满载目标：挂起后 CPU 增量 = **0**，恢复后继续增长 |
| 虚拟桌面迁移（可选） | 文档化 `IVirtualDesktopManager` | 单桌面返回 false（**多桌面路径未实测**） |
| 跳转列表清理（可选） | 按应用 `IApplicationDestinations` | 代码级（**未实测**），默认关闭 |
| 托盘图标隐藏 | Win10 经典 `ToolbarWindow32` + `TB_HIDEBUTTON` | 代码级；Win11 无 `ToolbarWindow32` 时优雅降级返回 false（**未实测**） |
| 快捷键修改界面 | 原生 Win32 窗口，录制式录入 + 冲突检测 | 窗口类注册 / 创建 / 销毁已验证；**点击级交互未验证** |

## 四、实现过程中发现并修复的缺陷

| 编号 | 缺陷 | 影响 | 修复 |
|---|---|---|---|
| P1 | `hide → hide → restore` 后窗口**永久隐藏** | 严重：任何"崩溃残留 + 再次隐藏"场景会让用户窗口永久丢失 | 落盘快照改为**权威**，重复隐藏只做**并集合并**，绝不整体替换 |
| P3 | 隐藏不存在的进程仍报成功 | 误导，且会污染快照状态 | 零匹配时返回失败码 2，不切状态、不写快照 |
| A1 | `--status` 跨进程恒报 `VISIBLE/0` | 状态查询不可信 | 增加持久化快照只读回退 |
| A2 | CLI 输出中文乱码（`???`） | 可用性问题 | `SetConsoleOutputCP(CP_UTF8)` + 宽字符输出 |
| 加固 | 陈旧快照 + HWND 复用可能重现 P1 类问题 | 潜在：窗口永久丢失 | 新增剪枝纯函数：同 HWND 异 PID 判为陈旧丢弃并重新快照；**不在本轮观测中的条目一律保留** |
| 测试隔离（非产品缺陷） | `TestOrchestratorIdempotency` 在 `%APPDATA%\HideUtil\state.json` 已存在时误报失败 | 测试脆弱：断言依赖全局磁盘状态；此前"全绿"为 `%APPDATA%` 恰好干净的假象 | 经 `SetStateFile` 注入隔离文件路径，并在测试前后用 `DeleteFileW` 清理，断言不再依赖外部环境 |
| **P0（严重）** | 目标窗口线程不泵消息（OneNote UWP 生命周期挂起 / 进程被挂起 / 应用自身卡死）时，`RestoreWindow` 与 `SetToolWindow` 的同步 `SetWindowPos(SWP_FRAMECHANGED)` 与 `ShowWindow(SW_RESTORE)` 永久阻塞 UI 线程；强杀后未恢复的窗口因仍隐藏 + TOOLWINDOW + 任务栏按钮被移除而无法点开（WPS/OneNote 实测触发） | 严重：核心恢复路径死锁 + 强杀后目标窗口永久不可恢复 | （1）`WindowHider` 全面非阻塞化：所有目标窗口 `SetWindowPos` 加 `SWP_ASYNCWINDOWPOS`、`ShowWindow` 改 `ShowWindowAsync`；（2）操作前 `SendMessageTimeoutW(WM_NULL, SMTO_ABORTIFHUNG)` 探活，无响应即跳过；（3）`RestoreAll` 跳过并保留：无响应/不可恢复的窗口/进程保留进 remainder 重新 Persist，仅全部成功才清 `state.json`；（4）`HideAll` 顺序修正：所有窗口操作完成后才挂起进程，并改为两阶段落盘；（5）`SelfShell` 启动自愈：检测到遗留快照自动 `RestoreAll`。端到端验证：用户真实遗留的 200 窗口快照在 15s 内完整恢复 |
| **P1（严重）** | 恢复后窗口大小/位置/最大化丢失（变成屏幕居中、特定默认大小）；有时黑屏或不显示（需多次 hide/restore + 最小化其他窗口才恢复）。根因：`RestoreWindow` 在 `SetWindowPlacement`（已设正确位置+showCmd）之后又调 `ShowWindowAsync(SW_SHOW)` + `ShowWindowAsync(SW_RESTORE)`，两者把位置覆盖回默认；`SWP_ASYNCWINDOWPOS` 让帧重算异步 → DWM 合成滞后 | 严重：核心恢复体验崩坏，用户无法拿回原窗口布局 | （1）去掉 `ShowWindow(SW_SHOW/SW_RESTORE)`，让 `SetWindowPlacement` 的 `showCmd`（含 SW_SHOWMAXIMIZED 等）唯一决定显示态；（2）响应窗口的 `SetWindowPos` 改同步（去 `SWP_ASYNCWINDOWPOS`），`SWP_FRAMECHANGED` 立即生效；（3）追加 `RedrawWindow(RDW_INVALIDATE \| RDW_UPDATENOW \| RDW_FRAME)` 强制 DWM 重绘 |

> 快照语义约定已写入 `docs/02-构建与运行说明.md` 第 8.1 节，**违反任一条都会复现 P1**，后续改动请对照该节与 `tests/core_tests.cpp` 中的回归用例。

## 五、技术边界（设计中已明确、实现中如实遵守）

**已实现且可正常工作**：窗口/任务栏/Alt+Tab 隐藏、音频静音、自身无痕运行、窗口位置保真、进程挂起、快捷键修改界面。

**受限项（已如实降级，不做 hack）**：

- **Win11 他人托盘图标**：通知区域已改为 XAML 渲染，无受支持 API，检测不到 `ToolbarWindow32` 即返回 false。
- **独占模式 / ASIO 音频**：绕过会话音量控制，对其无效。
- **防截屏**：`SetWindowDisplayAffinity` 仅对调用进程自身窗口生效。
- **虚拟桌面**：文档化 API 无法创建新桌面，单桌面环境返回 false。

**不可实现（明确不提供，只解释原因）**：

- **在任务管理器中隐藏进程**（自身或目标）：用户态无任何受支持 API，实现必然落到内核驱动 + 绕过 DSE/PatchGuard/HVCI，属 rootkit 行为。**本方案明确拒绝实现。**
- **"隐藏"快捷键占用**：伪需求，系统无枚举他人热键的 API。
- **清除持久化取证痕迹**：需篡改系统，且清除行为本身即恶意特征。替代策略是"尽量减少痕迹产生"。
- **隐藏开机自启项**：与自启本质矛盾。

> 静态复查实证：`src/` 全目录中内核接口、进程隐藏、注册表写 API **均出现 0 次**。

## 六、需要你人工点验的清单

自动化环境无法覆盖以下部分（无 UAC 提权交互、无法模拟鼠标点击、无音频会话、单虚拟桌面），建议你手工确认：

1. **主程序启动**：双击 `build/HideUtil.exe` → 接受 UAC → 确认**没有任何窗口、托盘图标、控制台**出现。
2. **快捷键开关**：按 `Ctrl+Alt+F12` → 目标应用窗口消失；再按一次 → 回到原位。
3. **设置界面唤起**（三种都试）：按 `Ctrl+Alt+F11` / 再次双击 `HideUtil.exe` / 运行 `HideUtil.exe --settings`。
4. **修改快捷键**：在设置界面点「录制」→ 按新组合 → 保存 → 验证新组合立即生效，旧组合失效。
5. **冲突检测**：尝试录制一个已被系统或其它软件占用的组合，确认弹出提示。
6. **规则管理**：新增一条规则（目标 exe）→ 保存 → 用热键验证该应用被隐藏。
7. **音频静音**：目标选一个正在播放声音的应用，验证隐藏时静音、恢复时声音回来。
8. **管理员目标**：对以管理员身份运行的应用验证隐藏（这是提权运行的意义所在）。
9. **Win11 托盘**：确认托盘图标隐藏项按预期降级（不报错、不篡改系统）。

## 七、已知遗留事项

1. ~~工作区根目录有若干**临时验证文本文件**与 `NVIDIA Corporation/` 驱动日志目录~~ —— **已于 2026-09-11 清理完成**：删除了 `NVIDIA Corporation/`（驱动日志）、`build_qa/`、`build_qa2/`（冗余 QA 构建目录）与临时 `build_verify/`（验证用）。`.gitignore` 已包含 `build/`。
2. `build/` 为构建产物，已加入忽略规则。
3. 未做代码签名：`requireAdministrator` + 未签名 exe 可能触发 SmartScreen 提示。
4. `hideutil_tests` 与 `hideutil_qa_tests` 为开发期验证目标，可分发包中移除。
5. `%APPDATA%\HideUtil\state.json` 与 `config.json` 为运行时真实状态/配置（已被本次 P0 修复完整恢复并清理），属用户数据。

## 八、构建与运行

```bash
bash scripts/build.sh                       # 一键构建
build/hideutil_tests.exe                    # 单元测试
build/hideutil_cli.exe --hide notepad.exe   # 命令行隐藏
build/hideutil_cli.exe --restore            # 恢复
```

> 本机 CMake 的 `Visual Studio 18 2026` 生成器**识别不到编译器**，必须使用 Ninja + 手动注入 MSVC 环境变量（`scripts/build.sh` 已固化）。详见 `docs/02-构建与运行说明.md` 第 3 节。
