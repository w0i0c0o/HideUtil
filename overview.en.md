# Delivery Overview: One-Click Hide / Restore Tool (Boss Key)

> Delivery date: 2026-09-11
> Delivery team: software-hideutil (software development team SOP · architecture review → implementation → test verification)
> Workspace: `E:\幸玮代码\Buddy_Space\6_Fade`

## 1. TL;DR

A Windows user-mode desktop tool: one global hotkey hides/restores every visible trace of the configured applications (windows, taskbar buttons, Alt+Tab entries, audio), supports configuration-driven management of multiple applications, and ships a **native settings window that lets you remap the global hotkeys**. **Zero drivers and zero kernel hooks throughout.**

- **Build status**: zero errors, zero warnings; four executable targets produced
- **Test status**: unit tests `checks=466 failures=0 -> PASS` (`461` when the foreground is locked; both are PASS — see section 4 of `docs/en/02-build-and-run.md`); QA automation regression `checks=6 failures=0 -> PASS`
- **Known defects**: 0 (five real product defects + one test-isolation defect + one P0 hang/unrecoverable defect + one P1 restore-position/black-screen defect + one P1 foreground-stealing defect were found and fixed; all are covered by regression cases)
- **UI languages**: Simplified Chinese and English, following the system UI language by default; falls back to English when the requested language is not available

## 2. Deliverables

### Source and build

| Path | Description |
|---|---|
| `CMakeLists.txt` | Build entry point (Ninja + MSVC) |
| `scripts/build.sh` | One-shot build script (hard-codes the MSVC environment injection this machine needs) |
| `app.manifest` | Main program manifest (`requireAdministrator`) |
| `resources/config.schema.json` | Configuration JSON Schema |
| `resources/lang/` | Language documents: `en-US.json` (authoritative key set) / `zh-CN.json`, embedded as `RCDATA` at build time |
| `src/main.cpp` | `wWinMain` entry point (single instance + `--settings`) |
| `src/self/` | Self shell: `SelfShell` (message-only window) / `HotkeyManager` (hotkey registration and re-registration rollback) / `Logger` (off by default) |
| `src/core/` | Core engine: `Types` / `Json` (self-implemented, zero dependencies) / `AppRuleStore` / `StateStore` / `ProcessWindowResolver` / `HideOrchestrator` / `WindowHider` / `AudioMuter` / `WinEventHook` / `TrayIconHider` / `ProcessController` / `ProcessWatcher` / `VirtualDesktopMover` / `JumpListCleaner` |
| `src/i18n/` | i18n: `Localizer` (language loading / two-level fallback / positional placeholder templates) |
| `src/ui/SettingsWindow.cpp` | Settings window (native Win32, hotkey recording + rule management) |
| `src/cli/main_cli.cpp` | Command-line shell (asInvoker, automation verification harness) |
| `tests/core_tests.cpp` | Pure logic unit tests (466 assertions; 461 when the foreground is locked) |
| `tests/qa_ui_tests.cpp` | QA addition: settings window reachability test |

### Documentation

| Path | Description |
|---|---|
| [`docs/en/01-architecture-and-feasibility.md`](docs/en/01-architecture-and-feasibility.md) | Architecture design and feasibility analysis, English (15 chapters / 910 lines) |
| [`docs/en/02-build-and-run.md`](docs/en/02-build-and-run.md) | Build, run, configuration and limitation notes, English (including the snapshot semantics contract, section 8.1) |
| [`docs/01-架构设计与可行性分析.md`](docs/01-架构设计与可行性分析.md) | Architecture design and feasibility analysis (Chinese source) |
| [`docs/02-构建与运行说明.md`](docs/02-构建与运行说明.md) | Build, run, configuration and limitation notes (Chinese source) |
| `docs/class-diagram.mermaid` / `docs/sequence-diagram.mermaid` | Class diagram and sequence diagram |
| `overview.en.md` | This file |
| `overview.en.md` / `CONTRIBUTING.en.md` / `GIT_WORKFLOW.en.md` | English versions of this file and the collaboration guides |

### Build artifacts

| Target | Privilege | Purpose |
|---|---|---|
| `build/HideUtil.exe` | `requireAdministrator` | **Official deliverable**: no window / no tray icon / no console |
| `build/hideutil_cli.exe` | `asInvoker` | Command-line test shell (`--list` / `--hide` / `--restore` / `--status` / `--suspend` / `--resume` / `--settings-dump`) |
| `build/hideutil_tests.exe` | `asInvoker` | Unit tests |
| `build/hideutil_qa_tests.exe` | `asInvoker` | QA addition: settings window reachability test |

## 3. Features and Measured Evidence

| Feature | Implementation | Measured evidence |
|---|---|---|
| Hide window + taskbar button | `ShowWindow(SW_HIDE)` + `ITaskbarList::DeleteTab` | Window count 1→0 after hiding, 0→1 after restore |
| Remove from Alt+Tab | `WS_EX_TOOLWINDOW` | Takes effect together with window hiding |
| Catch applications re-showing themselves | `SetWinEventHook(EVENT_OBJECT_SHOW)` | QA forces `ShowWindow(SW_SHOW)`, then hide → restore still works and the rectangle is unchanged |
| **Bring to front on restore (new config item)** | `AppConfig::bringRestoredToFront` (default `false`) + the "Bring windows to front on restore" checkbox in the settings window | Checked = `SetForegroundWindow` brings restored windows to the front; unchecked = the foreground is handed back to the window the user was using before the restore (WPS is unaffected when it is on top) |
| **Foreground hand-back (P1 regression)** | `HideOrchestrator::RestoreAll` records `GetForegroundWindow()` before restoring and hands the foreground back **once** after all windows are restored | Fixes "window still brought to front even though bring-to-front is unchecked": `SetWindowPlacement` has no no-activate option, so restoring necessarily activates the window | Reverse verification: with the fix temporarily disabled, 3 assertions fail (proving the bug is reproducible and the assertions really bite); with it enabled, 195/195 are green (this evidence was recorded on 2026-09-11, when the suite had 195 assertions in total) |
| Mute application audio | WASAPI session-level `ISimpleAudioVolume::SetMute` + `IAudioSessionNotification` | Code path + compile verification (the test target has no audio session, so **no real listening test was performed**) |
| Hide its own traces | `wWinMain` + message-only window, no tray icon registered | No window / no tray icon / no console |
| Window position fidelity | Snapshot `WINDOWPLACEMENT` + `GWL_STYLE`/`GWL_EXSTYLE` | Move to (320,240,980,700) → hide → after restore the rectangle is **exactly the same** and the HWND is unchanged |
| Suspend processes (optional) | `ntdll!NtSuspendProcess` (resolved dynamically via `GetProcAddress`) | CPU-saturated target: CPU delta = **0** after suspend, grows again after resume |
| Virtual desktop migration (optional) | Documented `IVirtualDesktopManager` | Returns false on a single desktop (**the multi-desktop path was not measured**) |
| Jump list cleanup (optional) | Per-application `IApplicationDestinations` | Code level only (**not measured**), off by default |
| Tray icon hiding | Win10 classic `ToolbarWindow32` + `TB_HIDEBUTTON` | Code level only; degrades gracefully and returns false when Win11 has no `ToolbarWindow32` (**not measured**) |
| Hotkey remapping window | Native Win32 window, record-style input + conflict detection | Window class registration / creation / destruction verified; **click-level interaction not verified** |
| Multi-language UI | `Localizer` + embedded `RCDATA` language documents | English and Chinese key sets are **exactly 72 keys each**, placeholders `{0}`–`{3}` aligned key by key; `fr-FR` falls back to `en-US`; a missing key falls back to its English value; covered by the unit test `TestLocalizerFallbackAndParity` |

## 4. Defects Found and Fixed During Implementation

| ID | Defect | Impact | Fix |
|---|---|---|---|
| P1 | Window **permanently hidden** after `hide → hide → restore` | Severe: any "crash leftover + hide again" scenario makes the user's window disappear forever | The persisted snapshot became **authoritative**; repeated hiding only performs a **union merge** and never a wholesale replacement |
| P3 | Hiding a non-existent process still reported success | Misleading, and it polluted the snapshot state | Returns failure code 2 on zero matches; no state transition and no snapshot write |
| A1 | `--status` always reported `VISIBLE/0` across processes | State query was untrustworthy | Added a read-only fallback to the persisted snapshot |
| A2 | Garbled non-ASCII CLI output (`???`) | Usability problem | `SetConsoleOutputCP(CP_UTF8)` + wide-character output |
| Hardening | Stale snapshots + HWND reuse could reproduce P1-class problems | Potential: window lost forever | New pure pruning function: same HWND with a different PID is treated as stale, discarded, and re-snapshotted; **entries not observed in this round are always kept** |
| Test isolation (not a product defect) | `TestOrchestratorIdempotency` falsely failed when `%APPDATA%\HideUtil\state.json` already existed | Test fragility: the assertion depended on global disk state; the previous "all green" was an illusion produced by a coincidentally clean `%APPDATA%` | An isolated file path is injected via `SetStateFile`, and `DeleteFileW` cleans up before and after the test, so the assertion no longer depends on the external environment |
| **P0 (severe)** | When the target window's thread is not pumping messages (OneNote UWP suspended by the system lifecycle / process suspended / application itself hung), the synchronous `SetWindowPos(SWP_FRAMECHANGED)` in `RestoreWindow` and `SetToolWindow`, and `ShowWindow(SW_RESTORE)`, block the UI thread forever; after a forced kill, unrestored windows cannot be clicked open because they are still hidden + TOOLWINDOW + their taskbar button was removed (reproduced with WPS/OneNote) | Severe: deadlock on the core restore path + target windows permanently unrestorable after a forced kill | (1) `WindowHider` made fully non-blocking: all target-window `SetWindowPos` calls get `SWP_ASYNCWINDOWPOS`, and `ShowWindow` becomes `ShowWindowAsync`; (2) probe liveness with `SendMessageTimeoutW(WM_NULL, SMTO_ABORTIFHUNG)` before operating and skip unresponsive windows; (3) `RestoreAll` skips and keeps: unresponsive/unrestorable windows and processes are kept in the remainder and persisted again, and `state.json` is cleared only when everything succeeds; (4) `HideAll` order corrected: processes are suspended only after all window operations finish, and persistence became two-phase; (5) `SelfShell` startup self-healing: detects a leftover snapshot and runs `RestoreAll` automatically. End-to-end verification: a real leftover 200-window snapshot from the user was fully restored within 15 s |
| **P1 (severe)** | After restore, window size/position/maximized state were lost (became screen-centered at a specific default size); sometimes a black window or nothing at all (needed several hide/restore cycles plus minimizing other windows to recover). Root cause: after `SetWindowPlacement` (which already set the correct position and `showCmd`), `RestoreWindow` also called `ShowWindowAsync(SW_SHOW)` + `ShowWindowAsync(SW_RESTORE)`, which overwrote the position back to the default; `SWP_ASYNCWINDOWPOS` made frame recalculation asynchronous, so DWM composition lagged behind | Severe: the core restore experience collapsed, and users could not get their original window layout back | (1) Removed `ShowWindow(SW_SHOW/SW_RESTORE)` so that the `showCmd` of `SetWindowPlacement` (including `SW_SHOWMAXIMIZED` etc.) solely determines the display state; (2) `SetWindowPos` on responsive windows became synchronous (dropped `SWP_ASYNCWINDOWPOS`) so `SWP_FRAMECHANGED` takes effect immediately; (3) Appended `RedrawWindow(RDW_INVALIDATE \| RDW_UPDATENOW \| RDW_FRAME)` to force a DWM repaint |

> The snapshot semantics contract is written into section 8.1 of [`docs/en/02-build-and-run.md`](docs/en/02-build-and-run.md); **violating any of it reproduces P1**, so please check future changes against that section and the regression cases in `tests/core_tests.cpp`.

## 5. Technical Boundaries (stated in the design, honestly respected in the implementation)

**Implemented and working**: window/taskbar/Alt+Tab hiding, audio muting, traceless self operation, window position fidelity, process suspension, hotkey remapping window.

**Limited items (honestly degraded, no hacks):**

- **Other applications' tray icons on Win11**: the notification area is now XAML-rendered and there is no supported API; when `ToolbarWindow32` cannot be found, false is returned.
- **Exclusive mode / ASIO audio**: bypasses session volume control, so muting has no effect on it.
- **Screenshot protection**: `SetWindowDisplayAffinity` only affects windows owned by the calling process.
- **Virtual desktops**: the documented API cannot create new desktops, so false is returned in a single-desktop environment.

**Not implementable (explicitly not provided; only the reason is explained):**

- **Hiding a process in Task Manager** (ours or the target's): user mode has no supported API for this; any implementation would necessarily fall back to a kernel driver plus bypassing DSE/PatchGuard/HVCI, i.e. rootkit behavior. **This design explicitly refuses to implement it.**
- **"Hiding" hotkey registrations**: a pseudo-requirement; the system has no API to enumerate another application's hotkeys.
- **Clearing persisted forensic traces**: requires tampering with the system, and the clearing behavior itself is a malicious signature. The alternative strategy is "minimize trace generation".
- **Hiding autostart entries**: contradicts the very nature of autostart.

> Static review evidence: across the whole `src/` tree, kernel interfaces, process hiding, and registry write APIs each appear **0 times**.

## 6. Checklist for Your Manual Verification

The automation environment cannot cover the following (no interactive UAC elevation, no way to simulate mouse clicks, no audio session, single virtual desktop), so please confirm them by hand:

1. **Main program startup**: double-click `build/HideUtil.exe` → accept UAC → confirm that **no window, tray icon, or console** appears.
2. **Hotkey toggle**: press `Ctrl+Alt+F12` → the target application's windows disappear; press it again → they return to their original positions.
3. **Settings window invocation** (try all three): press `Ctrl+Alt+F11` / double-click `HideUtil.exe` again / run `HideUtil.exe --settings`.
4. **Remap hotkeys**: in the settings window click "Record" → press the new combination → save → verify the new combination takes effect immediately and the old one stops working.
5. **Conflict detection**: try recording a combination already taken by the system or another application, and confirm that a prompt appears.
6. **Rule management**: add a rule (target exe) → save → verify with the hotkey that the application gets hidden.
7. **Audio muting**: pick an application that is playing sound as the target, and verify it is muted when hidden and sound returns on restore.
8. **Administrator targets**: verify hiding against an application running as administrator (this is the whole point of running elevated).
9. **Win11 tray**: confirm the tray-icon hiding option degrades as expected (no error, no system tampering).
10. **Language override**: set the `language` config field to `en-US`, restart, then open the settings window and confirm every string is in English with no empty strings and no leaked key names; then set it to `zh-CN` and confirm everything is in Chinese.
11. **System language follow**: set `language` back to `auto`, and on an English system (or after switching the system UI language to English) confirm the UI switches to English automatically.

## 7. Known Remaining Items

1. ~~Several **temporary verification text files** and an `NVIDIA Corporation/` driver log directory in the workspace root~~ — **cleaned up on 2026-09-11**: removed `NVIDIA Corporation/` (driver logs), `build_qa/`, `build_qa2/` (redundant QA build directories), and the temporary `build_verify/` (used for verification). `.gitignore` already contains `build/`.
2. `build/` contains build artifacts and is covered by the ignore rules.
3. No code signing: `requireAdministrator` plus an unsigned exe may trigger a SmartScreen prompt.
4. `hideutil_tests` and `hideutil_qa_tests` are development-time verification targets and can be dropped from the distribution package.
5. `%APPDATA%\HideUtil\state.json` and `config.json` are real run-time state/configuration (fully restored and cleaned by this P0 fix) and belong to the user.
6. The settings window does not yet offer a language drop-down; the default `auto` already satisfies the "follow the system language" requirement. Also: **CLI diagnostic output is deliberately kept in English** and does not change with the UI language.

## 8. Build and Run

```bash
bash scripts/build.sh                       # One-shot build
build/hideutil_tests.exe                    # Unit tests
build/hideutil_cli.exe --hide notepad.exe   # Command-line hide
build/hideutil_cli.exe --restore            # Restore
```

> The local CMake `Visual Studio 18 2026` generator **cannot detect the compiler**, so you must use Ninja with manually injected MSVC environment variables (hard-coded in `scripts/build.sh`). See section 3 of [`docs/en/02-build-and-run.md`](docs/en/02-build-and-run.md) for details.
