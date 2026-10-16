// Core data model for HideUtil.
//
// All snapshot structures follow the design document section 3.3 exactly.
// This header intentionally depends only on <windows.h> and the C++ standard
// library so it can be shared by every module.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace hideutil {
namespace core {

// ---------------------------------------------------------------------------
// Original-state snapshots (section 3.3).
// ---------------------------------------------------------------------------

struct WindowSnapshot {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    BOOL wasVisible = FALSE;          // IsWindowVisible original value
    LONG_PTR prevStyle = 0;           // GetWindowLongPtr(GWL_STYLE) original
    LONG_PTR prevExStyle = 0;         // GetWindowLongPtr(GWL_EXSTYLE) original
    BOOL taskbarTabRemoved = FALSE;   // whether we removed the taskbar button
    WINDOWPLACEMENT placement{};      // position / size / show state
    BOOL wasForeground = FALSE;       // was the foreground window before hiding
    BOOL altTabSuppressed = FALSE;    // whether we inserted WS_EX_TOOLWINDOW

    // Batch 2 (T04) additions, kept per-window so restore stays paired.
    BOOL movedToDesktop = FALSE;      // whether we moved it to another desktop
    std::wstring prevDesktopId;       // GUID string of the original desktop
};

struct AudioSnapshot {
    std::wstring sessionId;           // IAudioSessionControl2 session identifier
    DWORD pid = 0;
    BOOL wasMuted = FALSE;            // original mute state
    float prevVolume = 1.0f;          // original volume
};

struct ProcessSnapshot {
    DWORD pid = 0;
    BOOL wasSuspended = FALSE;        // suspended by this tool
};

struct GlobalSnapshot {
    std::chrono::system_clock::time_point hiddenAt{};
    std::vector<WindowSnapshot> windows;
    std::vector<AudioSnapshot> audio;
    std::vector<ProcessSnapshot> processes;
    BOOL moveToVirtualDesktopApplied = FALSE;

    // PIDs whose jump list should be cleared when restoring (one-way cleanup,
    // batch 2 / T04; default off).
    std::vector<DWORD> jumpListPids;

    bool empty() const {
        return windows.empty() && audio.empty() && processes.empty();
    }
};

// ---------------------------------------------------------------------------
// Rule model (section 3.1).
// ---------------------------------------------------------------------------

enum class MatchMode { Any, All };

struct MatchSpec {
    std::optional<std::wstring> exePath;
    std::optional<std::wstring> processName;
    std::optional<std::wstring> windowClass;
    MatchMode matchMode = MatchMode::Any;
};

struct ActionSpec {
    bool hideWindow = true;
    bool hideTrayIcon = true;
    bool muteAudio = true;
    bool suspendProcess = false;
    bool moveToVirtualDesktop = false;
    bool hideFromAltTab = true;
    bool clearJumpList = false;
};

struct RestoreSpec {
    bool restoreWindowPosition = true;
    bool restoreForegroundWindow = true;
};

struct AppRule {
    std::wstring id;
    bool enabled = true;
    std::wstring name;
    MatchSpec match;
    ActionSpec actions;
    RestoreSpec restore;
};

// ---------------------------------------------------------------------------
// Hotkey / runtime / root config.
// ---------------------------------------------------------------------------

struct HotkeySpec {
    std::vector<std::wstring> modifiers;  // CTRL | ALT | SHIFT | WIN
    std::wstring key;                      // "F12" | "A" | "VK_0x7B"
    UINT modFlags = 0;                     // resolved MOD_* flags
    UINT vk = 0;                           // resolved virtual key code
};

struct HotkeyConfig {
    HotkeySpec hideToggle;                 // Ctrl+Alt+F12 by default
    HotkeySpec openSettings;               // Ctrl+Alt+F11 by default
};

struct RuntimeConfig {
    bool requireElevation = true;
    bool startHidden = true;
    bool registerAutostart = false;
    int pollIntervalMs = 1500;
};

struct AppConfig {
    int version = 1;
    HotkeyConfig hotkey;
    RuntimeConfig runtime;
    std::vector<AppRule> rules;

    // Global restore behaviour: when true, every successfully restored window
    // is brought to the foreground (SetForegroundWindow, best-effort). Default
    // false so the user's current foreground work is not yanked away when
    // hidden windows come back.
    bool bringRestoredToFront = false;

    // UI language: "auto" (follow the system UI language), "en-US" or "zh-CN".
    // See i18n/Localizer. Defaults to "auto".
    std::wstring language = L"auto";
};

// ---------------------------------------------------------------------------
// Resolution results.
// ---------------------------------------------------------------------------

// A resolved window plus the rule that matched it (may be null when used for
// ad-hoc enumeration such as `--list`).
struct WindowRef {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    const AppRule* rule = nullptr;
};

// Everything the orchestrator needs to hide / mute / tray-hide.
struct TargetSet {
    std::vector<WindowRef> windows;
    std::vector<DWORD> pids;              // all matched process ids
    std::vector<DWORD> mutePids;          // pids whose audio sessions must be muted
    std::vector<DWORD> trayPids;          // pids whose tray icons should be hidden
    std::vector<DWORD> suspendPids;       // pids to suspend (batch 2)
    std::vector<DWORD> desktopPids;       // pids to move to another desktop (batch 2)
    std::vector<DWORD> jumpListPids;      // pids whose jump list to clear on restore (batch 2)

    bool empty() const { return windows.empty() && pids.empty(); }
};

}  // namespace core
}  // namespace hideutil
