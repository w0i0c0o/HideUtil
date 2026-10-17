// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

#include "self/SelfShell.h"

#include "core/TargetEnumerator.h"
#include "self/InstanceHandoff.h"
#include "self/Logger.h"

#include "i18n/Localizer.h"

#include <commctrl.h>

#include <cstdio>

namespace hideutil {
namespace self {

namespace {

const wchar_t* kMutexName = L"Local\\HideUtil_SingleInstance_Mutex";
const wchar_t* kEventName = L"Local\\HideUtil_ShowSettings_Event";
const wchar_t* kMessageName = L"HideUtil_ShowSettings";

struct TargetStartedPayload {
    DWORD pid = 0;
    std::wstring name;
};

}  // namespace

SelfShell* SelfShell::instance_ = nullptr;
UINT SelfShell::showSettingsMessage_ = 0;

const wchar_t* SelfShell::SingleInstanceMutexName() { return kMutexName; }
const wchar_t* SelfShell::ShowSettingsEventName() { return kEventName; }
const wchar_t* SelfShell::ShowSettingsMessageName() { return kMessageName; }

SelfShell::SelfShell() : orchestrator_(ruleStore_, stateStore_, resolver_) {}

SelfShell::~SelfShell() = default;

int SelfShell::Run(HINSTANCE instance, bool showSettingsRequested, const std::wstring& page) {
    instance_ = this;
    hinstance_ = instance;
    showSettingsMessage_ = RegisterWindowMessageW(kMessageName);

    // Drop any wake request left over from a previous session: acting on it
    // would pop the settings window without the user asking, which is an
    // unnecessary exposure for a hiding tool.
    DiscardPendingWakeRequest();

    // E: record this instance (pid + module path) so a second launch can name
    // the running copy -- and end it -- if it ever stops responding.
    WriteOwnInstanceMarker();

    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&commonControls);

    // Load (or create) the per-user configuration.
    const std::wstring configPath = core::AppRuleStore::DefaultConfigPath();
    ruleStore_.EnsureDefaultConfig(configPath);
    ruleStore_.Load(configPath);

    // Resolve the UI language now that the config is available: "auto" follows
    // the system UI language and an undeveloped language falls back to English.
    // Before this point (the single-instance handoff further up) Localizer was
    // already initialized with the system default from wWinMain.
    i18n::Localizer::Initialize(ruleStore_.Config().language);

    // Persist the snapshot to the standard state file. Without this a crash (or
    // a forced kill) would leave the target windows hidden with no way back;
    // with it, the next run or `hideutil_cli --restore` can still put them back.
    orchestrator_.SetStateFile(core::StateStore::DefaultStatePath());

    if (!CreateMessageWindow(instance)) {
        Logger::Error(L"failed to create message-only window");
        instance_ = nullptr;
        return 1;
    }

    // Marshal the background watcher callback onto the UI thread.
    orchestrator_.SetTargetStartedHandler([this](DWORD pid, const std::wstring& name) {
        auto* payload = new TargetStartedPayload{pid, name};
        if (!PostMessageW(hwnd_, kMsgTargetStarted, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    });

    RegisterHotkeys();
    StartEventThread();

    // Crash safety net: if the previous run was killed (or crashed) while
    // targets were hidden, a leftover state.json exists. Restore everything
    // right now so no window is left unrecoverable. RestoreAll is idempotent,
    // never blocks on unresponsive targets and keeps anything it had to skip
    // in the snapshot for the next retry.
    if (orchestrator_.QueryPersistedState().hasSnapshot) {
        Logger::Info(L"leftover snapshot detected; restoring previous state on startup");
        orchestrator_.RestoreAll();
    }

    if (showSettingsRequested) {
        ShowSettings(page);
    }

    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) != 0) {
        if (result == -1) {
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    StopEventThread();
    settings_.Destroy();
    hotkeys_.UnregisterAll(hwnd_);
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    instance_ = nullptr;
    return 0;
}

bool SelfShell::CreateMessageWindow(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.lpfnWndProc = &SelfShell::WndProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    // A message-only window: parent HWND_MESSAGE, never shown, not in the task
    // bar, no tray icon, no console.
    hwnd_ = CreateWindowExW(0, kWindowClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance,
                            this);
    return hwnd_ != nullptr;
}

void SelfShell::RegisterHotkeys() {
    const core::HotkeyConfig& hotkeys = ruleStore_.Config().hotkey;
    if (!hotkeys_.ReRegister(hwnd_, HotkeyManager::kHotkeyHideToggle, hotkeys.hideToggle)) {
        if (hotkeys_.IsConflict(HotkeyManager::kHotkeyHideToggle)) {
            Logger::Warn(L"hideToggle hotkey already registered by another application");
        } else {
            Logger::Warn(L"failed to register hideToggle hotkey");
        }
    }
    if (!hotkeys_.ReRegister(hwnd_, HotkeyManager::kHotkeyOpenSettings, hotkeys.openSettings)) {
        if (hotkeys_.IsConflict(HotkeyManager::kHotkeyOpenSettings)) {
            Logger::Warn(L"openSettings hotkey already registered by another application");
        } else {
            Logger::Warn(L"failed to register openSettings hotkey");
        }
    }
}

bool SelfShell::ProbeHotkey(int slotId, const core::HotkeySpec& spec, bool* conflict) {
    if (conflict != nullptr) {
        *conflict = false;
    }
    const int tempId = 9000 + slotId;
    if (hotkeys_.Register(hwnd_, tempId, spec)) {
        hotkeys_.Unregister(hwnd_, tempId);
        return true;
    }
    if (conflict != nullptr) {
        *conflict = hotkeys_.IsConflict(tempId);
    }
    return false;
}

void SelfShell::HandleWakeRequest() {
    // C: the second instance parked its intent in wake-request.json. Consume it
    // (read + delete) so an old request can never be replayed on a later wake.
    // A missing or corrupt file degrades to "just open the settings window".
    WakeRequest request;
    const WakeRequestStatus status = ReadAndConsumeWakeRequest(WakeRequestPath(), request);
    if (status == WakeRequestStatus::Stale) {
        // A leftover request from a wake that was never acknowledged. Acting on
        // it would open the settings window without the user asking, so drop it
        // and leave the desktop exactly as it is. Deliberately no
        // acknowledgement: a stale request means there is no live second
        // instance waiting for one.
        return;
    }
    if (status == WakeRequestStatus::Missing) {
        request = WakeRequest{};
    }
    ShowSettings(request.page);

    // B: acknowledge ONLY now -- after the window has actually been shown. The
    // second instance waits on this to tell a healthy instance from a hung one.
    if (wakeAckEvent_ != nullptr) {
        SetEvent(wakeAckEvent_);
    }
}

void SelfShell::ShowSettings(const std::wstring& page) {
    ui::SettingsHost host;
    host.suspendHotkeys = [this]() { hotkeys_.UnregisterAll(hwnd_); };
    host.resumeHotkeys = [this]() { RegisterHotkeys(); };
    host.onConfigChanged = [this]() {
        RegisterHotkeys();
        // The orchestrator holds a reference to ruleStore_, so a re-load is
        // immediate; re-register the hotkeys above to apply new combinations.
    };
    host.probeHotkey = [this](int slotId, const core::HotkeySpec& spec, bool* conflict) {
        return ProbeHotkey(slotId, spec, conflict);
    };
    host.enumerateTargets = [this]() { return core::EnumerateTargetCandidates(resolver_); };
    host.onQuitApp = [this]() { QuitApplication(); };
    settings_.Show(hinstance_, ruleStore_, std::move(host), page);
}

void SelfShell::QuitApplication() {
    // 1. Put everything back the way we found it (idempotent).
    orchestrator_.RestoreAll();

    // 2. Ask the persisted snapshot whether anything is still outstanding. A
    //    successful restore deletes the state file, so whatever remains is
    //    genuinely still hidden and could not be restored.
    //
    //    The warning fires ONLY when THIS session actually hid windows. A
    //    state.json inherited from a previous crash / forced-exit (whose window
    //    the startup auto-restore could not bring back) must not nag the user
    //    on a quit where they hid nothing -- the recovery file is simply kept
    //    for the next launch's auto-restore retry. If there are no genuine
    //    leftovers at all, any stale state file is pruned so it cannot trigger
    //    the reminder on a later launch.
    const core::HideOrchestrator::PersistedStatus status = orchestrator_.QueryPersistedState();
    const bool genuineLeftovers =
        status.hasSnapshot && (status.windowCount > 0 || status.audioCount > 0);
    if (hidThisSession_ && genuineLeftovers) {
        const std::wstring detail = i18n::Localizer::Format(
            i18n::Localizer::Get("quit.warnBody"), std::to_wstring(status.windowCount),
            std::to_wstring(status.audioCount));
        MessageBoxW(settings_.Handle(), detail.c_str(),
                    i18n::Localizer::Get("quit.warnTitle").c_str(), MB_OK | MB_ICONWARNING);
    } else if (!genuineLeftovers) {
        // Nothing is actually hidden any more (everything was restored, or the
        // leftover entries were inert). Remove the stale state file so it does
        // not produce a spurious reminder on a future launch.
        DeleteFileW(core::StateStore::DefaultStatePath().c_str());
    }

    // 3. Tear down. The message loop in Run() performs the remaining cleanup
    //    (settings window, hotkeys, message window, event thread).
    PostQuitMessage(0);
}

void SelfShell::StartEventThread() {
    if (eventThreadStarted_) {
        return;
    }
    showSettingsEvent_ = CreateEventW(nullptr, FALSE, FALSE, kEventName);
    // B: created (not opened) so this works no matter which side started first.
    wakeAckEvent_ = CreateEventW(nullptr, FALSE, FALSE, WakeAckEventName());
    quitEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (showSettingsEvent_ == nullptr || quitEvent_ == nullptr) {
        // Nothing will start, so StopEventThread -- whose teardown is gated on
        // eventThreadStarted_ -- never runs. Release every handle created above
        // here, otherwise they are leaked for the lifetime of the process.
        if (showSettingsEvent_ != nullptr) {
            CloseHandle(showSettingsEvent_);
            showSettingsEvent_ = nullptr;
        }
        if (quitEvent_ != nullptr) {
            CloseHandle(quitEvent_);
            quitEvent_ = nullptr;
        }
        if (wakeAckEvent_ != nullptr) {
            // Best-effort handle: losing it just means a second instance falls
            // back to the timeout prompt instead of a clean handshake.
            CloseHandle(wakeAckEvent_);
            wakeAckEvent_ = nullptr;
        }
        return;
    }
    eventThreadStarted_ = true;
    eventThread_ = std::thread(&SelfShell::EventThreadMain, this);
}

void SelfShell::StopEventThread() {
    if (!eventThreadStarted_) {
        return;
    }
    if (quitEvent_ != nullptr) {
        SetEvent(quitEvent_);
    }
    if (eventThread_.joinable()) {
        eventThread_.join();
    }
    if (showSettingsEvent_ != nullptr) {
        CloseHandle(showSettingsEvent_);
        showSettingsEvent_ = nullptr;
    }
    if (wakeAckEvent_ != nullptr) {
        CloseHandle(wakeAckEvent_);
        wakeAckEvent_ = nullptr;
    }
    if (quitEvent_ != nullptr) {
        CloseHandle(quitEvent_);
        quitEvent_ = nullptr;
    }
    eventThreadStarted_ = false;
}

void SelfShell::EventThreadMain() {
    HANDLE handles[2] = {showSettingsEvent_, quitEvent_};
    while (true) {
        const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0) {
            PostMessageW(hwnd_, showSettingsMessage_, 0, 0);
        } else {
            break;  // quitEvent_ or error
        }
    }
}

LRESULT CALLBACK SelfShell::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (instance_ != nullptr && showSettingsMessage_ != 0 && message == showSettingsMessage_) {
        // Runs on the UI thread, so the acknowledgement below is only sent once
        // the settings window has really been brought forward.
        instance_->HandleWakeRequest();
        return 0;
    }
    switch (message) {
        case WM_HOTKEY:
            if (instance_ != nullptr) {
                instance_->OnHotkey(static_cast<int>(wParam));
            }
            return 0;
        case SelfShell::kMsgTargetStarted:
            if (instance_ != nullptr) {
                instance_->OnTargetStartedMessage(lParam);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

void SelfShell::OnTargetStartedMessage(LPARAM lParam) {
    auto* payload = reinterpret_cast<TargetStartedPayload*>(lParam);
    if (payload == nullptr) {
        return;
    }
    orchestrator_.OnTargetProcessStarted(payload->pid, payload->name);
    delete payload;
}

void SelfShell::OnHotkey(int id) {
    switch (id) {
        case HotkeyManager::kHotkeyHideToggle:
            if (orchestrator_.GetState() == core::HideOrchestrator::State::Hidden) {
                // Manual restore: the hide->restore cycle is closed. Clear the
                // flag so a state.json inherited from a PREVIOUS crashed session
                // (which the startup auto-restore could not bring back) does not
                // keep this session "sticky" and fire a spurious quit warning.
                orchestrator_.RestoreAll();
                hidThisSession_ = false;
            } else {
                // Only mark the session as having hidden something when a hide
                // actually engaged (HideAll returns true). A no-match hotkey
                // press must not arm the quit warning.
                if (orchestrator_.HideAll()) {
                    hidThisSession_ = true;
                }
            }
            break;
        case HotkeyManager::kHotkeyOpenSettings:
            ShowSettings();
            break;
        default:
            break;
    }
}

}  // namespace self
}  // namespace hideutil
