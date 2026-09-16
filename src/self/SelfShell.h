// Self shell: windowless, tray-less, console-less process entry.
// Creates a message-only window (HWND_MESSAGE) to receive WM_HOTKEY, owns the
// single-instance logic and hosts the on-demand settings window.
#pragma once

#include "core/AppRuleStore.h"
#include "core/HideOrchestrator.h"
#include "core/ProcessWindowResolver.h"
#include "core/StateStore.h"
#include "self/HotkeyManager.h"
#include "ui/SettingsWindow.h"

#include <windows.h>

#include <string>
#include <thread>

namespace hideutil {
namespace self {

class SelfShell {
public:
    SelfShell();
    ~SelfShell();

    SelfShell(const SelfShell&) = delete;
    SelfShell& operator=(const SelfShell&) = delete;

    // showSettingsRequested: the process was launched with --settings.
    // `page` selects which part of the settings window takes focus ("rules" /
    // "hotkeys"); empty means the default.
    int Run(HINSTANCE instance, bool showSettingsRequested,
            const std::wstring& page = std::wstring());

    // Show / focus the settings window (created on demand).
    void ShowSettings(const std::wstring& page = std::wstring());

    // Called when a second instance wakes us: consume its request file, act on
    // it, then acknowledge so the other process knows we are alive.
    void HandleWakeRequest();

    // Restore everything that was hidden, then terminate the process. Invoked
    // by the settings window's "退出 HideUtil" button.
    void QuitApplication();

    // Names used for the single-instance mutex and the inter-process wake-up.
    static const wchar_t* SingleInstanceMutexName();
    static const wchar_t* ShowSettingsEventName();
    static const wchar_t* ShowSettingsMessageName();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateMessageWindow(HINSTANCE instance);
    void OnHotkey(int id);
    void RegisterHotkeys();
    void OnTargetStartedMessage(LPARAM lParam);

    void StartEventThread();
    void StopEventThread();
    void EventThreadMain();

    bool ProbeHotkey(int slotId, const core::HotkeySpec& spec, bool* conflict);

    HWND hwnd_ = nullptr;
    HINSTANCE hinstance_ = nullptr;

    HotkeyManager hotkeys_;
    core::AppRuleStore ruleStore_;
    core::StateStore stateStore_;
    core::ProcessWindowResolver resolver_;
    core::HideOrchestrator orchestrator_;
    ui::SettingsWindow settings_;

    HANDLE showSettingsEvent_ = nullptr;  // auto-reset, signaled by 2nd instance
    HANDLE wakeAckEvent_ = nullptr;       // auto-reset, set after a wake is handled
    HANDLE quitEvent_ = nullptr;          // manual-reset, stops the wait thread
    std::thread eventThread_;
    bool eventThreadStarted_ = false;

    // Set the moment the user actively hides windows (hotkey), so the quit
    // warning only fires for windows hidden THIS session -- never for a
    // leftover state.json inherited from a previous crash / forced-exit.
    bool hidThisSession_ = false;

    static SelfShell* instance_;
    static constexpr wchar_t kWindowClassName[] = L"HideUtilMessageWindow";
    static constexpr UINT kMsgTargetStarted = WM_APP + 1;
    static UINT showSettingsMessage_;
};

}  // namespace self
}  // namespace hideutil
