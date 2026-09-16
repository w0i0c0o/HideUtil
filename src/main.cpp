// Windowless entry point for the HideUtil GUI deliverable.
#include "self/SelfShell.h"

#include <windows.h>

#include <cwchar>
#include <string>

namespace {

bool CommandLineHasSwitch(const wchar_t* commandLine, const wchar_t* switchName) {
    if (commandLine == nullptr || switchName == nullptr) {
        return false;
    }
    const std::wstring line = commandLine;
    return line.find(switchName) != std::wstring::npos;
}

// Wake the already-running instance so it opens the settings window, then exit.
void SignalExistingInstance(HWND hwndOwner, const wchar_t* eventName, const wchar_t* messageName) {
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
    if (event != nullptr) {
        SetEvent(event);
        CloseHandle(event);
    }
    const UINT message = RegisterWindowMessageW(messageName);
    if (message != 0) {
        // Secondary channel; message-only windows do not receive broadcasts, so
        // the named event above is the reliable signal.
        PostMessageW(HWND_BROADCAST, message, 0, 0);
    }
    (void)hwndOwner;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR pCmdLine,
                    int /*nCmdShow*/) {
    const bool showSettings = CommandLineHasSwitch(pCmdLine, L"--settings");

    // Single instance: the first process owns the mutex; later ones just wake it.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, hideutil::self::SelfShell::SingleInstanceMutexName());
    const bool alreadyRunning = (mutex != nullptr) && (GetLastError() == ERROR_ALREADY_EXISTS);
    if (alreadyRunning) {
        SignalExistingInstance(nullptr, hideutil::self::SelfShell::ShowSettingsEventName(),
                               hideutil::self::SelfShell::ShowSettingsMessageName());
        CloseHandle(mutex);
        return 0;
    }

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(hr);

    int exitCode = 1;
    {
        // SelfShell owns the orchestrator, which owns COM objects. Destroy it
        // while the STA apartment is still initialised.
        hideutil::self::SelfShell shell;
        exitCode = shell.Run(hInstance, showSettings);
    }

    if (comInitialized) {
        CoUninitialize();
    }
    if (mutex != nullptr) {
        CloseHandle(mutex);
    }
    return exitCode;
}
