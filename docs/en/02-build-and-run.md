# HideUtil Build and Run Guide (Batch 1 + Batch 2)

> Scope: T01 project infrastructure and self shell / T02 data model and core scheduling / T03 hide action handler core
> 　　　　/ T04 extended handlers and robustness monitoring / T05 settings window
> Compliance statement: this tool **only uses public user-mode (User Mode) APIs — zero drivers, zero kernel hooks**. It contains no kernel-side means of "hiding a process".
> Process suspension uses `ntdll!NtSuspendProcess/NtResumeProcess` (publicly exported, called from user mode); **ntdll is not statically linked** — the entry points are resolved at run time via `GetProcAddress`.

---

## 1. Directory Structure

```
6_Fade/
├── CMakeLists.txt                 # Build entry point (Ninja + MSVC)
├── app.manifest                   # HideUtil main program manifest (requireAdministrator)
├── resources/
│   └── config.schema.json         # Configuration file JSON Schema
├── scripts/
│   └── build.sh                   # One-shot build script (Git Bash)
├── src/
│   ├── main.cpp                   # wWinMain entry point (single-instance mutex + --settings switch)
│   ├── self/                      # Self shell
│   │   ├── SelfShell.h/.cpp       #   message-only window (HWND_MESSAGE) + message loop + single-instance signal thread
│   │   ├── HotkeyManager.h/.cpp   #   RegisterHotKey / conflict detection / ReRegister (run-time re-registration + rollback)
│   │   └── Logger.h/.cpp          #   Off by default; Debug builds route to OutputDebugString
│   ├── core/                      # Core engine (static library hideutil_core)
│   │   ├── Types.h                #   Snapshot / rule data model
│   │   ├── Json.h/.cpp            #   Self-contained lightweight JSON (zero third-party dependencies)
│   │   ├── AppRuleStore.h/.cpp    #   config.json load / save / rule matching
│   │   ├── StateStore.h/.cpp      #   Snapshot Begin/Add/Commit/Load/Clear/Persist (incl. desktop/suspend/jump list persistence)
│   │   ├── ProcessWindowResolver.h/.cpp  # PID↔HWND collection (incl. UWP); collects suspend/desktop/jumpList PIDs per rule action
│   │   ├── HideOrchestrator.h/.cpp       # State machine + scheduling + QueryPersistedState (read-only persisted snapshot query)
│   │   ├── WindowHider.h/.cpp            # Window hide/restore + taskbar + TOOLWINDOW
│   │   ├── AudioMuter.h/.cpp             # WASAPI session muting + session notifications
│   │   ├── WinEventHook.h/.cpp           # EVENT_OBJECT_SHOW safety net
│   │   ├── TrayIconHider.h/.cpp          # Win10 classic tray (graceful degradation on Win11)
│   │   ├── ProcessController.h/.cpp      # [T04] NtSuspendProcess/NtResumeProcess suspend/resume
│   │   ├── ProcessWatcher.h/.cpp         # [T04] Polling process monitor (std::thread + atomic + interruptible wait)
│   │   ├── VirtualDesktopMover.h/.cpp    # [T04] IVirtualDesktopManager move to another desktop
│   │   └── JumpListCleaner.h/.cpp        # [T04] Per-application jump list cleanup via IApplicationDestinations
│   ├── ui/                        # [T05] Settings window
│   │   └── SettingsWindow.h/.cpp  #   Native Win32 settings dialog (hotkey recording + rule list + save/cancel)
│   ├── cli/
│   │   └── main_cli.cpp           # Command-line test shell (asInvoker, no elevation; UTF-8 console output)
│   └── i18n/                      # Localization
│       └── Localizer.h/.cpp       #   Language resolution + two-level fallback (see "Internationalization")
└── tests/
    └── core_tests.cpp             # Pure logic unit tests (asInvoker)
```

---

## 2. Three Build Targets

| Target | Type | Subsystem | Privilege | Purpose |
|---|---|---|---|---|
| **HideUtil** | exe | WINDOWS (no console) | `requireAdministrator` | **Official deliverable**. No window, no tray icon, no console; only registers global hotkeys and works silently; single instance |
| **hideutil_cli** | exe | CONSOLE | `asInvoker` (no elevation) | **Automation verification harness**. Processes at the same user/medium integrity level are not subject to UIPI, so it can hide ordinary targets such as `notepad` |
| **hideutil_tests** | exe | CONSOLE | `asInvoker` (no elevation) | Pure logic unit tests: JSON round trip, rule matching, snapshot access, hotkey encode/decode, config round trip, suspend pairing semantics, state machine idempotency |

All core logic is compiled into the static library **`hideutil_core`**, and the three executable targets reuse the same implementation. The new T04/T05 source files are also added to `hideutil_core` (`src/ui/SettingsWindow.cpp` is compiled into the library as well, for later reuse).

---

## 3. Building

### 3.1 Prerequisites (verified on this machine)

- Visual Studio 18 BuildTools (MSVC `14.51.36231`, x64)
- Windows SDK `10.0.26100.0`
- CMake + Ninja (located in `C:/Users/WW/.workbuddy/binaries/python/envs/default/Scripts/`)

> ⚠️ The local CMake `Visual Studio 18 2026` generator **cannot detect the compiler** (it reports `No CMAKE_CXX_COMPILER could be found`).
> **You must use Ninja with manually injected MSVC environment variables**; the recipe lives in `scripts/build.sh`.

### 3.2 One-Shot Build

```bash
bash scripts/build.sh
```

This is equivalent to:

```bash
export PATH="/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64:/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64:$PATH"
export INCLUDE="C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.51.36231/include;C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/ucrt;C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/shared;C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/um;C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/winrt;C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/cppwinrt"
export LIB="C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.51.36231/lib/x64;C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/ucrt/x64;C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/um/x64"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Measured result: zero errors, zero warnings**, producing:

```
build/HideUtil.exe           ~150 KB
build/hideutil_cli.exe       ~130 KB
build/hideutil_tests.exe     ~170 KB
build/hideutil_core.lib      ~3.1 MB (incl. T04/T05)
```

Compile options: `/utf-8 /W4 /permissive-`, C++20; linked libraries `user32 ole32 oleaut32 comctl32 shlwapi shcore shell32 advapi32 uuid` (all via `target_link_libraries`; `#pragma comment(lib,...)` is **not** used).

> **No new third-party dependencies**: T04 process suspension resolves `NtSuspendProcess`/`NtResumeProcess` from `ntdll.dll` via `GetProcAddress` (**ntdll is not statically linked**); virtual desktops use the publicly documented COM interface `IVirtualDesktopManager`; jump lists use `IApplicationDestinations`. The T05 settings window is **native Win32** (no third-party UI library).

---

## 4. Running

### 4.1 HideUtil (the real program)

Double-click or run `build/HideUtil.exe` directly. Because the manifest declares `requireAdministrator`, a UAC prompt appears.

- No visible window, no tray icon, no console;
- Reads/creates `%APPDATA%\HideUtil\config.json`;
- Registers two global hotkeys:
  - `Ctrl+Alt+F12` — toggle hide / restore
  - `Ctrl+Alt+F11` — open the settings window
- **Single instance**: when started again, the new instance uses the named event `Local\HideUtil_ShowSettings_Event` plus a `RegisterWindowMessageW` broadcast to `HWND_BROADCAST` to tell the already-running instance to open its settings window, then exits itself (return code 0);
- Supports the command-line switch `--settings`: opens the settings window on startup (or, if already running, brings up the existing instance's settings window).

### 4.2 hideutil_cli (command-line test shell)

```bash
build/hideutil_cli.exe --list               # List visible top-level windows (process name / PID / window class / title)
build/hideutil_cli.exe --hide notepad.exe   # Hide and mute all matching notepad processes
build/hideutil_cli.exe --status             # Print state machine state + number of snapshotted windows (incl. persisted fallback)
build/hideutil_cli.exe --restore            # Restore the last hide (cross-process, reads state.json)
build/hideutil_cli.exe --suspend notepad.exe # [T04] Suspend matching processes (suspend-only rule, windows are not hidden)
build/hideutil_cli.exe --resume             # [T04] Resume suspended processes (alias semantics of --restore)
build/hideutil_cli.exe --settings-dump      # [T05] Print the current two hotkeys and all rules
```

> `--hide` / `--restore` pass the snapshot through `%APPDATA%\HideUtil\state.json`, so restore works correctly **across processes**.
> Console output uses `SetConsoleOutputCP(CP_UTF8)` + `WideCharToMultiByte` to emit UTF-8, so **non-ASCII window titles display correctly** (defect A2 fix).

### 4.3 hideutil_tests (unit tests)

```bash
build/hideutil_tests.exe
```

Measured output:

```
==== hideutil_tests ====
-- json round trip
-- rule matching (any / all)
-- state store snapshot
-- hotkey parsing
-- config save / load round trip
-- orchestrator state machine idempotency
-- hotkey encode / decode round trip
-- config round trip with openSettings
-- process suspend pairing semantics
-- snapshot merge preserves wasVisible (repeat hide)
-- snapshot merge keeps entries on empty observation
-- prune stale window (HWND recycled: same hwnd, new pid)
-- prune keeps entry when hwnd + pid both match
-- prune keeps entries not in this round's targets
-- prune result is a superset for same hwnd + pid
========================
checks=466 failures=0 -> PASS
```

> **Note: `checks=` is not a fixed number.** `TestRestoreDoesNotStealForeground` depends on whether the OS grants the foreground to the test process — five more assertions run when the foreground is available, giving `466`; when it is locked, `461`. **Both are PASS; seeing 461 is not a regression.** A further three assertions are skipped on machines where `HideAll()` fails.

---

## 5. Configuration File

Default path `%APPDATA%\HideUtil\config.json` (auto-generated on first run; nothing is written to the registry). Key fields:

```jsonc
{
  "version": 1,
  "language": "auto",
  "hotkey": {
    "hideToggle":   { "modifiers": ["CTRL","ALT"], "key": "F12" },
    "openSettings": { "modifiers": ["CTRL","ALT"], "key": "F11" }
  },
  "runtime": { "requireElevation": true, "startHidden": true, "registerAutostart": false, "pollIntervalMs": 1500 },
  "rules": [
    {
      "id": "rule-001", "enabled": true, "name": "Example",
      "match": { "exePath": null, "processName": "notepad.exe", "windowClass": null, "matchMode": "any" },
      "actions": { "hideWindow": true, "hideTrayIcon": true, "muteAudio": true,
                   "suspendProcess": false, "moveToVirtualDesktop": false,
                   "hideFromAltTab": true, "clearJumpList": false },
      "restore": { "restoreWindowPosition": true, "restoreForegroundWindow": true }
    }
  ]
}
```

- `language`: UI language — `auto` follows the system UI language, or force `en-US` / `zh-CN`. See [Internationalization](#internationalization).
- `matchMode`: `any` (applies when any field matches) / `all` (applies only when all fields match).
- `key` supports `F1`–`F24`, `A`–`Z`, `0`–`9`, `SPACE`/`TAB`/`ESC`, etc., or the `VK_0x7B` form.
- **`hotkey.openSettings` now fully supports save / load round trips** (defect fix C4, covered by a unit test).
- JSON is parsed by the built-in `core/Json` (**zero third-party dependencies**, about 400 lines, supports `\uXXXX` and surrogate pairs).

### 5.1 Data Model per Action (Batch 2)

- `suspendProcess` → `TargetSet.suspendPids` / `ProcessSnapshot.wasSuspended`: suspends on entering `HideAll`, and on leaving `RestoreAll` resumes **only processes with `wasSuspended == TRUE`**, avoiding accidentally resuming processes the user had already suspended.
- `moveToVirtualDesktop` → `WindowSnapshot.movedToDesktop` / `prevDesktopId`: moves to some existing "non-current" desktop and moves back on restore; **returns false when only one virtual desktop exists (no operation is performed)**.
- `clearJumpList` → top-level `GlobalSnapshot.jumpListPids`: calls `IApplicationDestinations::RemoveDestination` per application AppUserModelID; **does not clear the global "Recent documents"** (`SHAddToRecentDocs(SHARD_PATHW,nullptr)` is not used); returns false when unsupported; off by default.
- `hideWindow` / `hideTrayIcon` / `muteAudio` / `hideFromAltTab` → reuse the Batch 1 snapshot fields.

---

## 6. Settings Window (T05)

- **Trigger** (any one of three, all bring up the same instance's settings window):
  1. Global hotkey `Ctrl+Alt+F11`;
  2. Starting `HideUtil.exe` again (single-instance signal);
  3. Startup switch `--settings`.
- **Window form**: native Win32, **created on demand and destroyed on close** (not resident, to reduce resource usage).
- **Hotkey recording**: two rows (hide/restore, open settings); click to enter recording state; bare modifier keys are filtered out (at least one modifier + one main key is required); `Esc` cancels recording; a conflict check runs before saving (prompts when the combination is already taken).
- **Rule list**: shown in a `ListView` (name / match / actions), supports add / edit / delete; a modal child dialog edits the match fields and action checkboxes.
- **Save / Cancel**: Save writes back to `config.json` and **takes effect at run time** (`HotkeyManager::ReRegister` re-registers hotkeys; on failure it rolls back to the old combination); Cancel discards changes.
- **Log playback**: `Logger` is off by default.

---

## 7. Prerequisites and Limitations (read this)

- **UIPI**: a non-elevated process cannot manipulate high-integrity (administrator) target windows. The real program running as administrator covers the vast majority of targets; `hideutil_cli` is asInvoker and **can only manipulate same- or lower-integrity windows** (enough for automation verification).
- **Process suspension**: `NtSuspendProcess` is an **undocumented but long-stable public export**, called from user mode with no kernel code; suspend/resume are strictly paired, and processes the user had already suspended are never resumed.
- **Tray icon**: best-effort on the Win10 classic notification area (`ToolbarWindow32`) only; **when Win11 does not expose `ToolbarWindow32`, it degrades gracefully and returns `false` — no hack and no registry modification**.
- **Audio**: mutes the WASAPI session volume; **exclusive mode / ASIO bypasses session volume control**, so muting has no effect on them.
- **Virtual desktops**: only the publicly documented `IVirtualDesktopManager` is used (the undocumented `IVirtualDesktopManagerInternal` is **not** used); **returns false in a single-desktop environment**.
- **Jump lists**: cleaned per application only (`IApplicationDestinations`); **global recent documents are never touched**; returns false when unsupported.
- **Process monitoring**: uses **polling** (`CreateToolhelp32Snapshot` + interruptible `WaitForSingleObject`); WMI is deliberately not introduced (to avoid an extra dependency and thread complexity).
- **Compliance red line**: this tool is positioned as a legitimate "privacy shade / boss key"; it must never be used to evade monitoring or hide malicious behavior. **It does not hide the process itself** (only hides windows / mutes audio / optionally suspends).

---

## 8. Batch 2 Verification Conclusions

| Item | Method | Result |
|---|---|---|
| Clean incremental build | `bash scripts/build.sh` | Zero errors, zero warnings |
| Unit tests | `hideutil_tests.exe` | `checks=466 failures=0 -> PASS` (`461` when the foreground is locked; both PASS) |
| Defect A1 | `--hide notepad.exe` then `--status` from a **new process** | `state=HIDDEN hiddenWindows=N` ✅ |
| Defect A2 | `--list` | Non-ASCII titles display correctly (e.g. `无标题 - Notepad`) ✅ |
| Defect P1 | `hide → hide → restore` | Window **can be restored visible** after restore ✅ (permanently hidden before the fix) |
| Defect P3 | `--hide nosuchprocess12345.exe` | rc=2 + `no matching process found`, state.json byte-for-byte unchanged ✅ |
| Hardening: stale snapshots | HWND-reuse pruning (same hwnd, different pid discarded) | 113 unit checks incl. 4 pruning cases all pass ✅ (this evidence is from batch 2, when the suite had 113 checks in total) |
| T04 suspend | `--suspend` / `--resume` + CPU time sampling | CPU delta = 0 after suspend, grows again after resume ✅ |
| T04 window fidelity | Move off the default position, then hide→restore | Rectangle identical, HWND unchanged ✅ |
| T05 settings | `--settings-dump` | Both hotkeys + rules printed correctly ✅ |

> Note: **interactive** operation of the settings GUI (clicking record, editing rules) cannot be simulated in this automation environment; the config read/write path was verified via `--settings-dump`, and "window class registration / creation / destruction" was verified by a separate test target. Single-instance behaviour and hotkey invocation should be checked manually.

### 8.1 Snapshot Semantics (important — do not change casually)

Once a persisted snapshot (`state.json`) is created it is **authoritative**; repeated hiding may only perform a **union merge** and must never replace it wholesale:

- If the same `HWND` is already in the snapshot → **keep all of its fields as-is** (especially `wasVisible`), do not re-snapshot it; if the application re-shows the window itself, **use the stored entry** to hide it again, still without rewriting `wasVisible`.
- Same `HWND` but different `pid` → the handle is considered recycled: **discard the stale entry** and re-snapshot that window as new.
- A snapshot `HWND` that is **not** among this round's targets → **always keep it** (it may belong to another batch of hide targets, and deleting it would make it unrestorable forever).
- The only valid basis for "unconditional removal" is that the window has been destroyed (`IsWindow(hwnd) == FALSE`); **never** use "whether it appears in this round's observed set" as the criterion.

> Violating any of the above reproduces the serious defect "window permanently unrestorable after being hidden" (P1 originated from this). Related regression cases live in `tests/core_tests.cpp`.

**Restore and unresponsive targets (P0 semantics, do not change casually):**

- Every cross-thread operation on a **target window** must be non-blocking: `SetWindowPos` always carries `SWP_ASYNCWINDOWPOS`, and `ShowWindow` is replaced by `ShowWindowAsync`; before operating, probe whether the target thread is pumping messages with `SendMessageTimeoutW(WM_NULL, SMTO_ABORTIFHUNG)` (default 1000 ms, tunable via `WindowHider::SetProbeTimeoutMs`). Violating this causes **permanent UI-thread blocking (HideUtil hangs)** on suspended/hung targets (for example UWP windows suspended by the system lifecycle such as OneNote, or processes frozen by this tool).
- When `RestoreAll` meets an **unresponsive** window: **skip that window and continue restoring the rest**; never let a single window stall the whole operation. Skipped entries **stay in the persisted snapshot** (`state.json` is not deleted) so the next restore can retry them. Resuming suspended processes still happens **first** (a suspended thread does not pump messages, so the process must be resumed before the window can be restored).
- **Only when every restore in this round succeeds** (no remaining windows / processes) is the in-memory snapshot cleared and `state.json` deleted; otherwise the remaining entries are persisted again.
- During a hide pass, **suspending processes is always the last step** (after all window-hiding operations complete); otherwise window operations on already-suspended threads could never complete.
- If HideUtil detects a leftover snapshot at startup (previous crash / forced kill), it **performs one automatic restore** to return everything to its original state.

**Restore display state strictly uses `SetWindowPlacement` (P1 fix):**

- The display state in `WindowHider::RestoreWindow` is determined **solely** by the `showCmd` field of `SetWindowPlacement` (`SW_SHOWNORMAL` / `SW_SHOWMAXIMIZED` / `SW_SHOWMINIMIZED` / `SW_HIDE`); the original `WINDOWPLACEMENT` is captured with `GetWindowPlacement` at hide time.
- The restore path is **forbidden** from issuing `ShowWindow(SW_SHOW)` / `ShowWindow(SW_RESTORE)`: both force the window back to the "centered default size", overwriting the position/size just restored and producing a centering flicker — this is the root cause of issue 1 (position/size lost after restore, maximized state lost).
- `SetWindowPos` on a responsive window (one that passed the liveness probe) runs **synchronously** (no `SWP_ASYNCWINDOWPOS`) so that `SWP_FRAMECHANGED` takes effect immediately; after the restore completes, `RedrawWindow(..., RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME)` is appended to force a DWM repaint, avoiding issue 2 (black window / not displayed).
- `!wasVisible` path: keeps `ShowWindowAsync(SW_HIDE)` (asynchrony is harmless when the window stays hidden); the `wasVisible` path no longer issues any `ShowWindow`.

**"Bring windows to front on restore" switch (new feature):**

- Config item `restore.bringToFront` (at root level, sibling of `runtime.*`), default `false` — restore does not steal the current foreground window. When enabled, `RestoreAll` calls `SetForegroundWindow` for each successfully restored window (best effort; the system may refuse because of the foreground lock, and silent failure is acceptable).
- The settings window has a checkbox of the same name: "Bring windows to front on restore (leave unchecked to keep the current desktop z-order and avoid stealing the foreground)".
- The change covers only "bring to front after restore"; it does not replace the P0 liveness probe + skip-and-keep + startup self-healing, and does not modify the `HideAll` order or the two-phase persistence.

**Foreground hand-back (P1 regression fix, foreground stolen from the user when `bringToFront=false`):**

- `SetWindowPlacement` has **no** no-activate option (unlike `SetWindowPos`'s `SWP_NOACTIVATE` or `ShowWindow`'s `SW_SHOWNOACTIVATE`): its `showCmd` (`SW_SHOWNORMAL` / `SW_SHOWMAXIMIZED`) is internally equivalent to `ShowWindow` and therefore **necessarily activates the window and brings it to the foreground**. So merely skipping `SetForegroundWindow` is not enough — with `bringToFront=false` the window is still pushed to the foreground by `SetWindowPlacement`, stealing the window the user is working in (e.g. WPS).
- The fix lives in `HideOrchestrator::RestoreAll` (**not** per-window inside `WindowHider::RestoreWindow`, which would make the foreground jump around and flicker when restoring multiple windows):
  1. Before restoring (after `stateStore_.Load()`, before resuming processes) record `const HWND originalForeground = GetForegroundWindow();`. A hidden window cannot be the foreground window, so this is necessarily "the window the user is currently using".
  2. After **all** windows have been restored, hand the foreground back **once**: if `bringRestoredToFront == false` and `originalForeground` is still valid and is no longer the current foreground, call `SetForegroundWindow(originalForeground)`; if that fails (foreground lock), fall back to `BringWindowToTop(originalForeground)` to at least repair the z-order.
- Silent failure: when the foreground lock refuses, no dialog is shown and the user is not disturbed. Do **not** introduce `AttachThreadInput` for this (it carries a hang risk).
- When `bringToFront=true`, the hand-back is skipped (the user explicitly asked for restored windows on top).

---

## Internationalization

HideUtil's UI is localized. The design keeps the deliverable a single portable executable while guaranteeing that no user ever sees an empty string.

**Language selection**

The default is `auto`, which follows the system UI language (Chinese sublanguages resolve to `zh-CN`, everything else to `en-US`). Users can override this with the `language` field in `%APPDATA%\HideUtil\config.json`:

| `language` | Effect |
|---|---|
| `auto` (default) | Follow the system UI language; falls back to `en-US` when the system language is not one of the shipped languages |
| `en-US` | Force English |
| `zh-CN` | Force Simplified Chinese |

The JSON schema for this field lives in `resources/config.schema.json` (`language` is an enum of the three values above).

**Language documents**

Language documents live in `resources/lang/`; two are currently shipped:

| File | Role |
|---|---|
| `resources/lang/en-US.json` | English — the **authoritative key set** |
| `resources/lang/zh-CN.json` | Simplified Chinese |

At build time they are embedded into the executable as `RCDATA` resources (resource IDs `201` / `202`, defined in `src/ui/AppResources.h` and referenced from `resources/HideUtil.rc`). Nothing is read from disk at run time, so single-file portability is unaffected.

**Fallback rules (two levels)**

| Level | Situation | Result |
|---|---|---|
| Language level | The requested language is not shipped / not developed | English (`en-US`) |
| Key level | A key is missing from the active language | The English value for the same key |
| Key level | The key is missing from English as well | The key name itself (so a missing translation is visible in testing rather than silent) |

**Maintenance rules**

- **When adding or modifying any UI string, you must update the documents for all languages under `resources/lang/` in the same change.** English is the authoritative key set.
- Key-level completeness is enforced by the unit test `TestLocalizerFallbackAndParity()` in `tests/core_tests.cpp`. If the key sets drift apart, the test fails — treat it as a build gate, not a warning.

**Known trade-offs**

- The settings window does not yet offer a language drop-down; the default `auto` already satisfies the "follow the system language" requirement.
- **CLI diagnostic output is deliberately kept in English.** The badge tokens produced by the core layer (`fg` / `win` / `tray` / `admin` / `bg`) are language-neutral English tokens; the UI layer translates them for display, while the CLI prints them verbatim. This keeps automation scripts and log parsing stable across locales.
