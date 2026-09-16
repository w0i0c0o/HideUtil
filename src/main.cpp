// Windowless entry point for the HideUtil GUI deliverable.
#include "self/InstanceHandoff.h"
#include "self/SelfShell.h"

#include <windows.h>

#include <string>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR pCmdLine,
                    int /*nCmdShow*/) {
    using hideutil::self::LaunchExitCode;
    using hideutil::self::LaunchIntent;

    const std::wstring commandLine =
        (pCmdLine != nullptr) ? std::wstring(pCmdLine) : std::wstring();

    // C: classify the command line before anything else. State-changing
    // switches are refused for the first instance too -- they belong to
    // hideutil_cli.exe, and honouring them from a stray shortcut would let one
    // command reveal hidden windows, which is a back door into a privacy tool.
    const hideutil::self::ParsedLaunchArgs args = hideutil::self::ParseLaunchArgs(commandLine);
    if (args.intent == LaunchIntent::Rejected) {
        // Tell the user, then exit 4. Returning silently would be
        // indistinguishable from "the launch did nothing" -- the exact
        // silent-failure class this hand-off removes.
        return hideutil::self::ReportRejectedSwitch(args.rejectedSwitch);
    }

    // Single instance: the first process owns the mutex; later ones hand their
    // intent over instead of exiting silently.
    //
    // The `(mutex != nullptr)` guard is deliberate and must stay: if
    // CreateMutexW fails we prefer running a second copy over refusing to start
    // at all.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, hideutil::self::SelfShell::SingleInstanceMutexName());
    const bool alreadyRunning = (mutex != nullptr) && (GetLastError() == ERROR_ALREADY_EXISTS);
    if (alreadyRunning) {
        // B/C/E: forward the request, wait for the acknowledgement, and prompt
        // when the running instance turns out to be unresponsive.
        const hideutil::self::HandoffResult handoff =
            hideutil::self::RunSecondInstanceHandoff(commandLine);
        CloseHandle(mutex);
        return static_cast<int>(handoff.exitCode);
    }

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(hr);

    int exitCode = 1;
    {
        // SelfShell owns the orchestrator, which owns COM objects. Destroy it
        // while the STA apartment is still initialised.
        hideutil::self::SelfShell shell;
        exitCode = shell.Run(hInstance, args.explicitSettings, args.page);
    }

    if (comInitialized) {
        CoUninitialize();
    }
    if (mutex != nullptr) {
        CloseHandle(mutex);
    }
    return exitCode;
}
