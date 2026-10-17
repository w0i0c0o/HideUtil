# HideUtil

**One-click hide / restore for Windows — a Boss Key tool.**

---

## Download — you need exactly one file

> ### [`HideUtil.exe`](../../releases/latest/download/HideUtil.exe)
>
> **That is the whole thing.** This repository is source code and documentation; to *use* the tool
> you download **one executable** and nothing else. No installer, no Visual C++ Redistributable,
> no runtime, no DLLs, no configuration files to copy.

**Three steps:**

1. Download [`HideUtil.exe`](../../releases/latest/download/HideUtil.exe) from the
   [latest release](../../releases/latest).
2. Double-click it and accept the UAC prompt — it needs administrator rights to manage elevated
   windows. **Nothing appears: no window, no tray icon, no console.** That is by design.
3. Press `Ctrl+Alt+F12` to hide the applications you configured, `Ctrl+Alt+F11` to open settings.

Each release lists a **SHA-256** for the executable — worth checking before you run a downloaded
binary. [All releases →](../../releases/latest)

> Not sure whether this is what you want? Read
> [Scope and limitations](#scope-and-limitations) first. In short: HideUtil hides the *visible
> traces* of running applications. It deliberately cannot hide a process from Task Manager.

---

## What it does

HideUtil is a Windows user-mode desktop utility. A single global hotkey hides every visible
trace of the applications you configured — windows, taskbar buttons, Alt+Tab entries, tray icons
and audio — and a second press brings them back exactly where they were, maximized state and
geometry included.

**Zero drivers. Zero kernel hooks. Only supported, public user-mode APIs.**

---

## Features

| Area | What it does |
|---|---|
| Windows | Hides top-level windows and their taskbar buttons (`ShowWindow` + `ITaskbarList::DeleteTab`) |
| Alt+Tab | Removes entries via `WS_EX_TOOLWINDOW` |
| Re-show watchdog | `SetWinEventHook(EVENT_OBJECT_SHOW)` re-hides windows an application shows by itself |
| Audio | Mutes per-application audio at the WASAPI session level |
| Position fidelity | Snapshots `WINDOWPLACEMENT` + window styles; restores geometry **and** maximized state |
| Processes | Optional `NtSuspendProcess` freeze (resolved dynamically from `ntdll`) |
| Tray icons | Hides its own icon; Win10 classic tray icons of other apps on a best-effort basis |
| Its own traces | No window, no tray icon, no console |
| Hotkeys | Native settings window with record-style hotkey capture and conflict detection |
| Rules | Rule-based, multi-application configuration (exe path / process name / window class) |
| Languages | English and Simplified Chinese; follows the system UI language by default |

Restore is **non-blocking**: every cross-thread window operation is gated on an
`IsWindowResponsive` probe (`SendMessageTimeoutW(WM_NULL, SMTO_ABORTIFHUNG)`), so a target that
stops pumping messages can never deadlock HideUtil.

---

## Scope and limitations

This project states honestly what it does not do. These are design decisions, not bugs.

**Implemented and working** — window/taskbar/Alt+Tab hiding, audio muting, window position
fidelity, process suspension, hotkey remapping.

**Degraded on purpose** (no hacks):

- **Tray icons of other apps on Windows 11** — the notification area is XAML-rendered and exposes
  no supported API; the call returns `false` instead of tampering with the system.
- **Exclusive-mode / ASIO audio** — bypasses session volume control, so muting has no effect.
- **Screenshot protection** — `SetWindowDisplayAffinity` only affects windows owned by the calling
  process.
- **Virtual desktops** — the documented API cannot create desktops; returns `false` in a
  single-desktop environment.

**Not implemented, and will not be** (only the reason is given, never a workaround):

- **Hiding a process in Task Manager** — user mode has no supported API; any implementation would
  require a kernel driver and bypassing DSE/PatchGuard/HVCI, i.e. rootkit behaviour.
- **"Hiding" hotkey registrations** — a pseudo-requirement; Windows cannot enumerate another
  application's hotkeys.
- **Clearing persisted forensic traces** — requires tampering with the OS, and the clearing itself
  is a malicious signature. The chosen strategy is "minimize trace generation".
- **Hiding autostart entries** — contradicts the very nature of autostart.

> **Note on shutdown:** hidden applications still appear in Windows' "apps blocking shutdown" list.
> That list is produced by Windows from each application's response to `WM_QUERYENDSESSION`, which
> the target application itself controls. Hiding visual traces does not change it.

---

## Requirements

- Windows 10 / 11 x64 (user mode; the main executable requests administrator rights to cross UIPI)
- Visual Studio Build Tools with MSVC 14.5x
- Windows SDK 10.0.26100.0 (or a compatible version)
- CMake 3.20+ and Ninja

## Build

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

> **Build note:** run the **link** step from a native shell (PowerShell or `cmd`). Under Git Bash,
> `TMP` is a POSIX path (`/tmp`) that `link.exe` cannot resolve, and the link fails with
> `LNK1104: cannot open file 'C:\WINDOWS\lnk{...}.tmp'`. Compiling from Git Bash is fine — only
> linking is affected.

Four targets are produced:

| Target | Purpose |
|---|---|
| `build/HideUtil.exe` | **The deliverable.** No window, no tray icon, no console |
| `build/hideutil_cli.exe` | Command-line shell for automation and diagnostics |
| `build/hideutil_tests.exe` | Unit tests |
| `build/hideutil_qa_tests.exe` | Settings-window reachability tests |

### Run the tests

```powershell
build\hideutil_tests.exe      # checks=466 failures=0 -> PASS
build\hideutil_qa_tests.exe   # checks=6   failures=0 -> PASS
```

> The reported `checks=` number is **not a constant**: `TestRestoreDoesNotStealForeground` depends
> on whether the OS grants the foreground to the test process, so the total is `466` when the
> foreground is available and `461` when it is locked. Both are PASS.

---

## Usage

### Hotkeys (defaults)

| Hotkey | Action |
|---|---|
| `Ctrl+Alt+F12` | Toggle hide / restore |
| `Ctrl+Alt+F11` | Open the settings window |

The settings window can also be opened by launching `HideUtil.exe` a second time (the running
instance is woken via the single-instance mutex), or with `HideUtil.exe --settings`.

### Command line

```powershell
hideutil_cli.exe --list                 # enumerate visible top-level windows
hideutil_cli.exe --targets              # enumerate candidate target applications
hideutil_cli.exe --hide notepad.exe     # hide
hideutil_cli.exe --restore              # restore everything
hideutil_cli.exe --status               # current state
hideutil_cli.exe --settings-dump        # dump the effective configuration
```

CLI diagnostic output is **deliberately always English**, independent of the UI language, so logs
and scripts do not drift with the locale.

---

## Configuration

The configuration lives at `%APPDATA%\HideUtil\config.json` and is generated on first run. Nothing
is written to the registry, and auto-start is not registered by default.

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

Full field reference: [`docs/en/02-build-and-run.md`](docs/en/02-build-and-run.md) (section 5).

**Changing `language` requires restarting HideUtil** — the localizer is initialized once at
startup and does not hot-reload. Restart with:

```powershell
taskkill /F /IM HideUtil.exe
```

---

## Portability

`build/HideUtil.exe` is **a single portable file**. The CRT is linked statically (`/MT`), the
application icon and the language documents are embedded as resources (`ICON` / `RCDATA`), and
`config.json` / `state.json` are created under `%APPDATA%\HideUtil\` on demand.

`dumpbin /dependents` reports only system DLLs, so **no Visual C++ Redistributable is needed**:

```
USER32.dll  ole32.dll  COMCTL32.dll  SHELL32.dll
ADVAPI32.dll  dwmapi.dll  KERNEL32.dll  GDI32.dll
```

To migrate to another 64-bit Windows machine, copy `HideUtil.exe` alone. (Copy `config.json` too
only if you want to carry your rules across.) The executable is unsigned, so SmartScreen may warn
on first launch.

---

## Documentation

| Document | Contents |
|---|---|
| [`docs/en/01-architecture-and-feasibility.md`](docs/en/01-architecture-and-feasibility.md) | Architecture design and feasibility analysis |
| [`docs/en/02-build-and-run.md`](docs/en/02-build-and-run.md) | Build, run, configuration, limitations |
| [`docs/class-diagram.mermaid`](docs/class-diagram.mermaid), [`docs/sequence-diagram.mermaid`](docs/sequence-diagram.mermaid) | Class and sequence diagrams |
| [`overview.en.md`](overview.en.md) | Delivery overview |
| [`CONTRIBUTING.en.md`](CONTRIBUTING.en.md), [`GIT_WORKFLOW.en.md`](GIT_WORKFLOW.en.md) | Contribution and branching rules |

Chinese versions: [`docs/01-架构设计与可行性分析.md`](docs/01-架构设计与可行性分析.md),
[`docs/02-构建与运行说明.md`](docs/02-构建与运行说明.md), [`overview.md`](overview.md),
[`CONTRIBUTING.md`](CONTRIBUTING.md), [`GIT_WORKFLOW.md`](GIT_WORKFLOW.md).

---

## License

**`GPL-3.0-or-later`** — see [`LICENSE`](LICENSE).

HideUtil is licensed under the GNU General Public License v3 **or, at your option, any later
version** published by the Free Software Foundation. You may use, modify and distribute it,
including commercially, provided that any derivative work is released under the same terms
(`GPL-3.0-or-later`) with source made available.

Copyright (C) 2026 Xing Wei.

> Why `or-later` and not `GPL-3.0-only`: GPLv3 section 14 says that when a program specifies a
> numbered version *without* the "or later" grant, only that version applies. This project grants
> the upgrade explicitly, so the SPDX identifier is `GPL-3.0-or-later`. The same `or any later
> version` grant is stated at the top of `LICENSE`, and every source file carries a machine-readable
> `SPDX-License-Identifier` header, so the grant survives even if a single file is copied out of
> this repository.
