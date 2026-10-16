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
//   --wintree <exe|pid>   dump the full window tree (children included) of a
//                         process: visibility / style / geometry for every pane
#include "core/AppRuleStore.h"
#include "core/HideOrchestrator.h"
#include "core/ProcessWindowResolver.h"
#include "core/StateStore.h"
#include "core/TargetEnumerator.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <map>
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
        "  hideutil_cli --targets [filter]\n"
        "  hideutil_cli --wintree <exeName|pid>\n");
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
    // Diagnostic output is deliberately hard-coded English: it must not shift
    // with the UI language the user picked in config.language.
    OutLine(L"language=" + config.language);
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

// ---- --wintree: full recursive window-tree dump for one process ----------
//
// Diagnostic tool for composite UIs (Office ribbon, IDE docks). It prints
// EVERY window of the target process -- children included -- with visibility,
// style and geometry, so "which pane disappeared after hide/restore" can be
// answered by diffing two dumps instead of guessing.

struct WinTreeContext {
    std::vector<DWORD> pids;
    int dumped = 0;
};

bool WinTreePidMatches(const WinTreeContext& ctx, DWORD pid) {
    for (DWORD candidate : ctx.pids) {
        if (candidate == pid) {
            return true;
        }
    }
    return false;
}

// Optional per-window annotation taken from the persisted snapshot
// (state.json), so `--wintree` also shows exactly what HideUtil recorded for
// each window. This is the decisive evidence when a hide/restore anomaly has
// to be diagnosed from a single dump.
std::map<HWND, std::wstring> g_snapshotAnnotation;

void LoadSnapshotAnnotations() {
    g_snapshotAnnotation.clear();
    StateStore store;
    if (!store.RestoreFromFile(StateStore::DefaultStatePath())) {
        return;
    }
    for (const hideutil::core::WindowSnapshot& window : store.Snapshot().windows) {
        wchar_t text[192] = {};
        swprintf_s(text, L"  snap[wasVisible=%d showCmd=%u flags=0x%04X prevStyle=0x%08X]",
                   window.wasVisible ? 1 : 0,
                   static_cast<unsigned>(window.placement.showCmd),
                   static_cast<unsigned>(window.placement.flags),
                   static_cast<unsigned>(window.prevStyle));
        g_snapshotAnnotation[window.hwnd] = text;
    }
}

void DumpWindowNode(HWND hwnd, int depth) {
    wchar_t className[256] = {};
    GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));
    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));

    RECT rect{};
    GetWindowRect(hwnd, &rect);
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const BOOL visible = IsWindowVisible(hwnd);
    const HWND root = GetAncestor(hwnd, GA_ROOT);
    // Matches the re-hide policy: only the root of a window tree is ever
    // touched by the EVENT_OBJECT_SHOW watchdog.
    const bool rehiddable = (root == hwnd);

    std::wstring indent(static_cast<std::size_t>(depth) * 2, L' ');
    wchar_t line[1024] = {};
    swprintf_s(line,
               L"%ls%ls0x%-14p vis=%d rehide=%d style=0x%08X ex=0x%08X "
               L"rect=(%ld,%ld,%ld,%ld) [%3ldx%3ld] class=%-24ls %ls",
               indent.c_str(), depth == 0 ? L"ROOT " : L"  +- ", hwnd, visible,
               rehiddable ? 1 : 0, static_cast<unsigned>(style),
               static_cast<unsigned>(exStyle), rect.left, rect.top, rect.right, rect.bottom,
               rect.right - rect.left, rect.bottom - rect.top, className, title);
    const auto annotation = g_snapshotAnnotation.find(hwnd);
    if (annotation != g_snapshotAnnotation.end()) {
        wcscat_s(line, annotation->second.c_str());
    }
    OutLine(line);
}

// Direct children only. EnumChildWindows would return ALL descendants, which
// duplicates every pane once per ancestor when combined with recursion.
void DumpChildrenRecursive(HWND parent, int depth) {
    for (HWND child = GetWindow(parent, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        DumpWindowNode(child, depth);
        DumpChildrenRecursive(child, depth + 1);
    }
}

BOOL CALLBACK WinTreeEnumProc(HWND hwnd, LPARAM lParam) {
    WinTreeContext* ctx = reinterpret_cast<WinTreeContext*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || !WinTreePidMatches(*ctx, pid)) {
        return TRUE;
    }
    ++ctx->dumped;
    DumpWindowNode(hwnd, 0);
    DumpChildrenRecursive(hwnd, 1);
    return TRUE;
}

// First pass: collect the pids whose image name contains `wanted`.
// EnumWindows covers top-level windows whether or not they are visible, so a
// target that is currently HIDDEN is still matched.
struct PidCollectContext {
    std::wstring wanted;
    std::vector<DWORD> pids;
};

BOOL CALLBACK PidCollectProc(HWND hwnd, LPARAM lParam) {
    PidCollectContext* ctx = reinterpret_cast<PidCollectContext*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return TRUE;
    }
    for (DWORD seen : ctx->pids) {
        if (seen == pid) {
            return TRUE;
        }
    }
    const std::wstring base = hideutil::core::ToLowerCopy(
        ProcessWindowResolver::GetProcessBaseName(
            ProcessWindowResolver::GetProcessImagePath(pid)));
    if (base.find(ctx->wanted) != std::wstring::npos) {
        ctx->pids.push_back(pid);
    }
    return TRUE;
}

int CommandWinTree(const std::wstring& filter) {
    if (filter.empty()) {
        OutLine(L"wintree: missing argument (process image name or pid)");
        return 1;
    }
    WinTreeContext ctx;
    // Accept a numeric pid ...
    const DWORD asPid = static_cast<DWORD>(_wtoi(filter.c_str()));
    if (asPid != 0) {
        ctx.pids.push_back(asPid);
    }
    // ... and/or an image-name match, so "EXCEL.EXE" works too (and still
    // matches while the target is hidden).
    PidCollectContext collect;
    collect.wanted = hideutil::core::ToLowerCopy(filter);
    EnumWindows(PidCollectProc, reinterpret_cast<LPARAM>(&collect));
    for (DWORD pid : collect.pids) {
        if (!WinTreePidMatches(ctx, pid)) {
            ctx.pids.push_back(pid);
        }
    }

    if (ctx.pids.empty()) {
        OutLine(L"wintree: no running process matches '" + filter + L"'");
        return 2;
    }
    OutLine(L"window tree for '" + filter + L"' (vis=IsWindowVisible, rehide=root of window tree)");
    LoadSnapshotAnnotations();
    EnumWindows(WinTreeEnumProc, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.dumped == 0) {
        OutLine(L"wintree: no window found for the matched pid(s)");
        return 2;
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
    if (command == L"--wintree") {
        if (argc < 3) { PrintUsage(); return 1; }
        return CommandWinTree(argv[2]);
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
