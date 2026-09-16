// [QA] qadriver.exe - config-driven driver that exercises exactly the GUI code
// paths (build_qa only, links hideutil_core):
//
//   qadriver hide          load %APPDATA%\HideUtil\config.json (like SelfShell)
//                          and run HideAll() -- i.e. what the GUI hotkey does.
//   qadriver autorestore   mimic SelfShell::Run() startup: detect a leftover
//                          persisted snapshot and RestoreAll() it.
//
// Exit code 0 = command completed (check the printed lines for semantics).
#include "core/AppRuleStore.h"
#include "core/HideOrchestrator.h"
#include "core/ProcessWindowResolver.h"
#include "core/StateStore.h"

#include <windows.h>

#include <cstdio>
#include <string>

using hideutil::core::AppRuleStore;
using hideutil::core::HideOrchestrator;
using hideutil::core::ProcessWindowResolver;
using hideutil::core::StateStore;

namespace {

int DoHide() {
    AppRuleStore store;
    const std::wstring configPath = AppRuleStore::DefaultConfigPath();
    if (!store.Load(configPath)) {
        std::printf("driver hide: config load FAILED path='%ls'\n", configPath.c_str());
        return 2;
    }
    std::printf("driver hide: config loaded, rules=%zu\n", store.Rules().size());

    StateStore stateStore;
    ProcessWindowResolver resolver;
    HideOrchestrator orchestrator(store, stateStore, resolver);
    orchestrator.SetStateFile(StateStore::DefaultStatePath());

    const DWORD t0 = GetTickCount();
    const bool ok = orchestrator.HideAll();
    const DWORD elapsed = GetTickCount() - t0;
    std::printf("driver hide: HideAll=%d state=%ls hiddenWindows=%zu elapsedMs=%lu\n",
                ok ? 1 : 0, orchestrator.StateName(), orchestrator.HiddenWindowCount(),
                elapsed);
    return ok ? 0 : 3;
}

int DoAutoRestore() {
    AppRuleStore store;
    StateStore stateStore;
    ProcessWindowResolver resolver;
    HideOrchestrator orchestrator(store, stateStore, resolver);
    orchestrator.SetStateFile(StateStore::DefaultStatePath());

    // Exactly the SelfShell::Run() startup seam.
    const HideOrchestrator::PersistedStatus persisted = orchestrator.QueryPersistedState();
    if (!persisted.hasSnapshot) {
        std::printf("driver autorestore: no leftover snapshot (nothing to do)\n");
        return 0;
    }
    std::printf("driver autorestore: leftover snapshot detected windows=%zu audio=%zu\n",
                persisted.windowCount, persisted.audioCount);
    const DWORD t0 = GetTickCount();
    const bool ok = orchestrator.RestoreAll();
    const DWORD elapsed = GetTickCount() - t0;
    const HideOrchestrator::PersistedStatus after = orchestrator.QueryPersistedState();
    std::printf("driver autorestore: RestoreAll=%d elapsedMs=%lu state=%ls "
                "remainingSnapshot=%d remainingWindows=%zu\n",
                ok ? 1 : 0, elapsed, orchestrator.StateName(),
                after.hasSnapshot ? 1 : 0, after.windowCount);
    return ok ? 0 : 3;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: qadriver hide | autorestore\n");
        return 1;
    }
    const std::wstring command = argv[1];
    SetConsoleOutputCP(CP_UTF8);
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comOk = SUCCEEDED(hr);
    int code = 1;
    if (command == L"hide") {
        code = DoHide();
    } else if (command == L"autorestore") {
        code = DoAutoRestore();
    } else {
        std::printf("qadriver: unknown command '%ls'\n", command.c_str());
    }
    if (comOk) {
        CoUninitialize();
    }
    std::fflush(stdout);
    return code;
}
