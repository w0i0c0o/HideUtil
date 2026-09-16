// hideutil_cli: console test shell (asInvoker, no elevation required).
//
// Commands:
//   --list                list visible top-level windows (exe / pid / title)
//   --hide <exeName>      hide + mute every process whose image name matches
//   --suspend <exeName>   suspend (freeze) every matching process (paired with --resume)
//   --restore             restore the previously hidden state (cross-process)
//   --resume              alias of --restore; resumes only processes we suspended
//   --status              report state (reads the persisted snapshot for a new process)
//   --settings-dump       print the effective config (hotkeys + rules)
//   --targets [filter]    list selectable target programs (windows / tray / processes),
//                         i.e. exactly what the settings picker shows
#include "core/AppRuleStore.h"
#include "core/HideOrchestrator.h"
#include "core/ProcessWindowResolver.h"
#include "core/StateStore.h"
#include "core/TargetEnumerator.h"

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

using hideutil::core::AppRule;
using hideutil::core::AppRuleStore;
using hideutil::core::HideOrchestrator;
using hideutil::core::MatchMode;
using hideutil::core::ProcessWindowResolver;
using hideutil::core::StateStore;
using hideutil::core::TargetCandidate;
using hideutil::core::WindowRef;

namespace {

// ---- UTF-8 console output (fixes Chinese title mojibake on the console) ----
void OutUtf8(const std::wstring& text) {
    if (text.empty()) {
        return;
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return;
    }
    std::string bytes(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), bytes.data(),
                        needed, nullptr, nullptr);
    std::fwrite(bytes.data(), 1, bytes.size(), stdout);
}

void OutLine(const std::wstring& text) { OutUtf8(text + L"\n"); }

std::wstring FormatHotkey(const hideutil::core::HotkeySpec& spec) {
    std::wstring result;
    for (const std::wstring& modifier : spec.modifiers) {
        result += modifier;
        result += L"+";
    }
    result += spec.key;
    return result;
}

void PrintUsage() {
    std::printf(
        "hideutil_cli - command line test shell\n"
        "Usage:\n"
        "  hideutil_cli --list\n"
        "  hideutil_cli --hide <exeName>\n"
        "  hideutil_cli --suspend <exeName>\n"
        "  hideutil_cli --restore\n"
        "  hideutil_cli --resume\n"
        "  hideutil_cli --status\n"
        "  hideutil_cli --settings-dump\n"
        "  hideutil_cli --targets [filter]\n");
}

void CommandList(ProcessWindowResolver& resolver) {
    const std::vector<WindowRef> windows = resolver.EnumerateVisibleTopLevelWindows();
    wchar_t header[64] = {};
    swprintf_s(header, L"visible top-level windows: %zu", windows.size());
    OutLine(header);
    for (const WindowRef& ref : windows) {
        const std::wstring exePath = ProcessWindowResolver::GetProcessImagePath(ref.pid);
        const std::wstring baseName = ProcessWindowResolver::GetProcessBaseName(exePath);
        wchar_t title[256] = {};
        GetWindowTextW(ref.hwnd, title, static_cast<int>(std::size(title)));
        wchar_t line[640] = {};
        swprintf_s(line, L"  pid=%-6lu %-24ls hwnd=0x%p  %ls", ref.pid, baseName.c_str(),
                   ref.hwnd, title);
        OutLine(line);
    }
}

int CommandHide(HideOrchestrator& orchestrator, AppRuleStore& store, const std::wstring& exeName) {
    AppRule rule;
    rule.id = L"cli-adhoc";
    rule.name = L"CLI ad-hoc rule";
    rule.match.processName = exeName;
    rule.match.matchMode = MatchMode::Any;
    rule.actions.hideWindow = true;
    rule.actions.muteAudio = true;
    rule.actions.hideFromAltTab = true;
    // Deliberately false: automated tests must not touch explorer.exe.
    rule.actions.hideTrayIcon = false;
    store.SetRules({rule});

    const bool ok = orchestrator.HideAll();
    if (!ok) {
        OutLine(L"hide '" + exeName + L"': no matching process found (nothing to hide)");
        return 2;
    }
    wchar_t line[256] = {};
    swprintf_s(line, L"hide '%ls': ok; windows snapshotted=%zu; state=%ls", exeName.c_str(),
               orchestrator.HiddenWindowCount(), orchestrator.StateName());
    OutLine(line);
    return 0;
}

int CommandSuspend(HideOrchestrator& orchestrator, AppRuleStore& store, const std::wstring& exeName) {
    AppRule rule;
    rule.id = L"cli-suspend";
    rule.name = L"CLI suspend-only rule";
    rule.match.processName = exeName;
    rule.match.matchMode = MatchMode::Any;
    // Suspend-only: do NOT touch windows / audio / tray.
    rule.actions.hideWindow = false;
    rule.actions.hideTrayIcon = false;
    rule.actions.muteAudio = false;
    rule.actions.hideFromAltTab = false;
    rule.actions.suspendProcess = true;
    store.SetRules({rule});

    const bool ok = orchestrator.HideAll();
    if (!ok) {
        OutLine(L"suspend '" + exeName + L"': no matching process found (nothing to suspend)");
        return 2;
    }
    wchar_t line[256] = {};
    swprintf_s(line, L"suspend '%ls': ok; state=%ls (paired with --resume)", exeName.c_str(),
               orchestrator.StateName());
    OutLine(line);
    return 0;
}

int CommandRestore(HideOrchestrator& orchestrator) {
    const bool ok = orchestrator.RestoreAll();
    wchar_t line[128] = {};
    swprintf_s(line, L"restore: %s; state=%ls", ok ? L"ok" : L"failed", orchestrator.StateName());
    OutLine(line);
    return 0;
}

int CommandStatus(HideOrchestrator& orchestrator) {
    // In-process state first; otherwise fall back to the persisted snapshot so a
    // brand-new process still reports HIDDEN correctly.
    if (orchestrator.GetState() == HideOrchestrator::State::Hidden) {
        wchar_t line[128] = {};
        swprintf_s(line, L"state=HIDDEN hiddenWindows=%zu", orchestrator.HiddenWindowCount());
        OutLine(line);
        return 0;
    }
    const HideOrchestrator::PersistedStatus persisted = orchestrator.QueryPersistedState();
    if (persisted.hasSnapshot) {
        wchar_t line[128] = {};
        swprintf_s(line, L"state=HIDDEN hiddenWindows=%zu", persisted.windowCount);
        OutLine(line);
        return 0;
    }
    OutLine(L"state=VISIBLE hiddenWindows=0");
    return 0;
}

int CommandSettingsDump(AppRuleStore& store) {
    const std::wstring configPath = AppRuleStore::DefaultConfigPath();
    store.Load(configPath);
    const hideutil::core::AppConfig& config = store.Config();

    OutLine(L"config path: " + configPath);
    {
        wchar_t line[128] = {};
        swprintf_s(line, L"version=%d rules=%zu", config.version, config.rules.size());
        OutLine(line);
    }
    OutLine(L"hotkey.hideToggle   = " + FormatHotkey(config.hotkey.hideToggle));
    OutLine(L"hotkey.openSettings = " + FormatHotkey(config.hotkey.openSettings));
    {
        wchar_t line[128] = {};
        swprintf_s(line, L"runtime.pollIntervalMs=%d", config.runtime.pollIntervalMs);
        OutLine(line);
    }
    for (std::size_t i = 0; i < config.rules.size(); ++i) {
        const AppRule& rule = config.rules[i];
        wchar_t line[256] = {};
        swprintf_s(line, L"rule[%zu] id=%ls enabled=%d name=%ls", i, rule.id.c_str(),
                   rule.enabled ? 1 : 0, rule.name.c_str());
        OutLine(line);
    }
    return 0;
}

int CommandTargets(const ProcessWindowResolver& resolver, const std::wstring& filter) {
    // Exactly the data set the settings-window picker presents.
    const std::vector<TargetCandidate> candidates =
        hideutil::core::EnumerateTargetCandidates(resolver);
    const std::wstring lower = hideutil::core::ToLowerCopy(filter);

    {
        wchar_t header[160] = {};
        swprintf_s(header, L"target candidates: %zu (filter='%ls')", candidates.size(),
                   filter.c_str());
        OutLine(header);
    }
    std::size_t shown = 0;
    for (const TargetCandidate& candidate : candidates) {
        if (!hideutil::core::MatchesFilter(candidate, lower)) {
            continue;
        }
        ++shown;
        wchar_t line[640] = {};
        swprintf_s(line, L"  pid=%-6lu %-26ls %-20ls %ls", candidate.pid,
                   candidate.processName.c_str(),
                   hideutil::core::CandidateBadges(candidate).c_str(),
                   candidate.exePath.c_str());
        OutLine(line);
    }
    {
        wchar_t footer[128] = {};
        swprintf_s(footer, L"shown: %zu", shown);
        OutLine(footer);
    }
    return 0;
}

int RunCommand(int argc, wchar_t** argv) {
    AppRuleStore store;
    StateStore stateStore;
    ProcessWindowResolver resolver;
    HideOrchestrator orchestrator(store, stateStore, resolver);
    orchestrator.SetStateFile(StateStore::DefaultStatePath());

    if (argc < 2) {
        PrintUsage();
        return 1;
    }
    const std::wstring command = argv[1];
    if (command == L"--list") {
        CommandList(resolver);
        return 0;
    }
    if (command == L"--hide") {
        if (argc < 3) { PrintUsage(); return 1; }
        return CommandHide(orchestrator, store, argv[2]);
    }
    if (command == L"--suspend") {
        if (argc < 3) { PrintUsage(); return 1; }
        return CommandSuspend(orchestrator, store, argv[2]);
    }
    if (command == L"--restore" || command == L"--resume") {
        return CommandRestore(orchestrator);
    }
    if (command == L"--status") {
        return CommandStatus(orchestrator);
    }
    if (command == L"--settings-dump") {
        return CommandSettingsDump(store);
    }
    if (command == L"--targets") {
        return CommandTargets(resolver, argc >= 3 ? argv[2] : L"");
    }
    PrintUsage();
    return 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(hr);

    // The orchestrator owns COM objects (ITaskbarList / WASAPI / desktop manager).
    // It must be destroyed while the apartment is still alive.
    const int exitCode = RunCommand(argc, argv);

    if (comInitialized) {
        CoUninitialize();
    }
    std::fflush(stdout);
    return exitCode;
}
