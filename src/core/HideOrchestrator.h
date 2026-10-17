// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

// Core scheduler: VISIBLE -> HIDING -> HIDDEN -> RESTORING -> VISIBLE.
#pragma once

#include "core/AppRuleStore.h"
#include "core/AudioMuter.h"
#include "core/JumpListCleaner.h"
#include "core/ProcessController.h"
#include "core/ProcessWatcher.h"
#include "core/ProcessWindowResolver.h"
#include "core/StateStore.h"
#include "core/TrayIconHider.h"
#include "core/Types.h"
#include "core/VirtualDesktopMover.h"
#include "core/WinEventHook.h"
#include "core/WindowHider.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace hideutil {
namespace core {

class HideOrchestrator {
public:
    enum class State { Visible, Hiding, Hidden, Restoring };

    // Probe outcome for one window during a restore pass.
    enum class WindowProbe { Responsive, Unresponsive, Destroyed };

    // Pure, Win32-free partition used by RestoreAll: splits `windows` into
    // `restorable` (probe says the owning thread is pumping) and `deferred`
    // (alive but unresponsive: kept in the snapshot for a later retry).
    // Destroyed windows are dropped -- they can never be restored. Injected
    // as a callback so unit tests can drive it without real windows.
    static void PartitionWindowsForRestore(const std::vector<WindowSnapshot>& windows,
                                           const std::function<WindowProbe(HWND)>& probe,
                                           std::vector<WindowSnapshot>& restorable,
                                           std::vector<WindowSnapshot>& deferred);

    // Read-only view of a snapshot persisted on disk. Kept separate from the
    // in-memory state machine so the two responsibilities never mix.
    struct PersistedStatus {
        bool hasSnapshot = false;
        std::size_t windowCount = 0;
        std::size_t audioCount = 0;
    };

    // Called on a background thread when a new target process appears.
    using TargetStartedHandler = std::function<void(DWORD pid, const std::wstring& name)>;

    HideOrchestrator(AppRuleStore& ruleStore, StateStore& stateStore,
                     ProcessWindowResolver& resolver);
    ~HideOrchestrator();

    HideOrchestrator(const HideOrchestrator&) = delete;
    HideOrchestrator& operator=(const HideOrchestrator&) = delete;

    // Idempotent: a no-op when already hidden.
    bool HideAll();
    // Idempotent: a no-op when already visible.
    bool RestoreAll();

    void OnTargetProcessStarted(DWORD pid, const std::wstring& processName);
    void OnWindowShown(HWND hwnd, DWORD pid);

    State GetState() const { return state_; }
    const wchar_t* StateName() const;
    std::size_t HiddenWindowCount() const;

    // Read-only query of the persisted snapshot (does not touch the state
    // machine). Uses stateFile_ when set, else the default state path.
    PersistedStatus QueryPersistedState() const;

    // Set by the owner (SelfShell) to marshal the background watcher callback
    // onto the UI thread. When unset the callback runs inline.
    void SetTargetStartedHandler(TargetStartedHandler handler) {
        targetStartedHandler_ = std::move(handler);
    }

    // Optional snapshot file enabling cross-process / crash recovery restore.
    void SetStateFile(const std::wstring& path) { stateFile_ = path; }
    const std::wstring& StateFile() const { return stateFile_; }
    std::wstring EffectiveStateFile() const;

private:
    // Authoritative base snapshot for a hide: the persisted snapshot when one
    // exists (cross-process), otherwise the in-memory committed snapshot.
    // Never mutates the state machine.
    GlobalSnapshot LoadBaseSnapshot() const;

    AppRuleStore& ruleStore_;
    StateStore& stateStore_;
    ProcessWindowResolver& resolver_;

    WindowHider windowHider_;
    AudioMuter audioMuter_;
    TrayIconHider trayHider_;
    WinEventHook winEventHook_;
    ProcessController processController_;
    ProcessWatcher processWatcher_;
    VirtualDesktopMover desktopMover_;
    JumpListCleaner jumpListCleaner_;

    State state_ = State::Visible;
    std::vector<DWORD> targetPids_;
    std::wstring stateFile_;
    TargetStartedHandler targetStartedHandler_;
};

}  // namespace core
}  // namespace hideutil
