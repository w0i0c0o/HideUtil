// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

#include "core/ProcessWindowResolver.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>

namespace hideutil {
namespace core {

namespace {

bool ContainsValue(const std::vector<DWORD>& values, DWORD value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool ContainsHwnd(const std::vector<HWND>& values, HWND value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void AddPidDedup(std::vector<DWORD>& values, DWORD value) {
    if (value != 0 && !ContainsValue(values, value)) {
        values.push_back(value);
    }
}

struct MatchContext {
    const AppRuleStore* store = nullptr;
    const AppRule* adhocRule = nullptr;  // used by ResolveByExeName
    TargetSet* out = nullptr;
    std::vector<HWND> seenHwnd;
};

// Determine the "real" target for a top-level window. For UWP frames the
// visible UI belongs to a child process (the app), while the frame window is
// owned by ApplicationFrameHost.exe. We therefore hide the frame but attribute
// it to the app PID.
void ResolveWindowIdentity(HWND hwnd, std::wstring& exePath, std::wstring& processName,
                           const std::wstring& windowClass, DWORD& outPid) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    outPid = pid;
    exePath = ProcessWindowResolver::GetProcessImagePath(pid);
    processName = ProcessWindowResolver::GetProcessBaseName(exePath);

    if (windowClass == L"ApplicationFrameWindow") {
        HWND child = FindWindowExW(hwnd, nullptr, L"Windows.UI.Core.CoreWindow", nullptr);
        if (child == nullptr) {
            child = FindWindowExW(hwnd, nullptr, nullptr, nullptr);
        }
        if (child != nullptr) {
            DWORD childPid = 0;
            GetWindowThreadProcessId(child, &childPid);
            if (childPid != 0 && childPid != pid) {
                outPid = childPid;
                exePath = ProcessWindowResolver::GetProcessImagePath(childPid);
                processName = ProcessWindowResolver::GetProcessBaseName(exePath);
            }
        }
    }
}

BOOL CALLBACK EnumTopLevelProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<MatchContext*>(lParam);

    const std::wstring windowClass = ProcessWindowResolver::GetWindowClass(hwnd);
    std::wstring exePath;
    std::wstring processName;
    DWORD pid = 0;
    ResolveWindowIdentity(hwnd, exePath, processName, windowClass, pid);

    const AppRule* rule = nullptr;
    if (ctx->store != nullptr) {
        rule = ctx->store->Match(exePath, processName, windowClass);
    } else if (ctx->adhocRule != nullptr &&
               AppRuleStore::RuleMatches(*ctx->adhocRule, exePath, processName, windowClass)) {
        rule = ctx->adhocRule;
    }

    if (rule == nullptr) {
        return TRUE;
    }

    if (!ContainsHwnd(ctx->seenHwnd, hwnd)) {
        ctx->seenHwnd.push_back(hwnd);
        WindowRef ref;
        ref.hwnd = hwnd;
        ref.pid = pid;
        ref.rule = rule;
        ctx->out->windows.push_back(ref);
    }
    AddPidDedup(ctx->out->pids, pid);
    if (rule->actions.muteAudio) {
        AddPidDedup(ctx->out->mutePids, pid);
    }
    if (rule->actions.hideTrayIcon) {
        AddPidDedup(ctx->out->trayPids, pid);
    }
    if (rule->actions.suspendProcess) {
        AddPidDedup(ctx->out->suspendPids, pid);
    }
    if (rule->actions.moveToVirtualDesktop) {
        AddPidDedup(ctx->out->desktopPids, pid);
    }
    if (rule->actions.clearJumpList) {
        AddPidDedup(ctx->out->jumpListPids, pid);
    }
    return TRUE;
}

struct OwnerContext {
    HWND owner = nullptr;
    std::vector<HWND>* out = nullptr;
};

BOOL CALLBACK EnumOwnerProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<OwnerContext*>(lParam);
    if (GetWindow(hwnd, GW_OWNER) == ctx->owner) {
        ctx->out->push_back(hwnd);
    }
    return TRUE;
}

struct PidCollectContext {
    DWORD pid = 0;
    std::vector<HWND>* out = nullptr;
};

BOOL CALLBACK EnumPidProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<PidCollectContext*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx->pid && !ContainsHwnd(*ctx->out, hwnd)) {
        ctx->out->push_back(hwnd);
    }
    return TRUE;
}

BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<PidCollectContext*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx->pid && !ContainsHwnd(*ctx->out, hwnd)) {
        ctx->out->push_back(hwnd);
    }
    return TRUE;
}

BOOL CALLBACK EnumVisibleProc(HWND hwnd, LPARAM lParam) {
    auto* out = reinterpret_cast<std::vector<WindowRef>*>(lParam);
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    int length = GetWindowTextLengthW(hwnd);
    if (length == 0 && GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;  // skip invisible-ish owned helpers
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    WindowRef ref;
    ref.hwnd = hwnd;
    ref.pid = pid;
    ref.rule = nullptr;
    out->push_back(ref);
    return TRUE;
}

}  // namespace

std::wstring ProcessWindowResolver::GetWindowClass(HWND hwnd) {
    wchar_t buffer[256] = {};
    const int len = GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    if (len <= 0) return std::wstring();
    return std::wstring(buffer, static_cast<std::size_t>(len));
}

std::wstring ProcessWindowResolver::GetProcessImagePath(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) return std::wstring();
    wchar_t buffer[MAX_PATH * 4] = {};
    DWORD size = static_cast<DWORD>(std::size(buffer));
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
        result.assign(buffer, size);
    }
    CloseHandle(process);
    return result;
}

std::wstring ProcessWindowResolver::GetProcessBaseName(const std::wstring& path) {
    const std::size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

bool ProcessWindowResolver::IsUwpFrame(HWND hwnd) {
    return GetWindowClass(hwnd) == L"ApplicationFrameWindow";
}

TargetSet ProcessWindowResolver::ResolveTargets(const AppRuleStore& store) const {
    TargetSet result;
    MatchContext ctx;
    ctx.store = &store;
    ctx.out = &result;
    EnumWindows(&EnumTopLevelProc, reinterpret_cast<LPARAM>(&ctx));

    // Pull in owned popups for every matched window.
    const std::vector<HWND> matched = ctx.seenHwnd;
    for (HWND hwnd : matched) {
        std::vector<HWND> owned = CollectOwnedPopups(hwnd);
        for (HWND ownedHwnd : owned) {
            if (ContainsHwnd(ctx.seenHwnd, ownedHwnd)) continue;
            DWORD ownedPid = 0;
            GetWindowThreadProcessId(ownedHwnd, &ownedPid);
            ctx.seenHwnd.push_back(ownedHwnd);
            WindowRef ref;
            ref.hwnd = ownedHwnd;
            ref.pid = ownedPid != 0 ? ownedPid : 0;
            // Reuse the owning window's rule.
            for (const auto& w : result.windows) {
                if (w.hwnd == hwnd) {
                    ref.rule = w.rule;
                    break;
                }
            }
            result.windows.push_back(ref);
        }
    }
    return result;
}

TargetSet ProcessWindowResolver::ResolveByExeName(const std::wstring& exeName) const {
    AppRule adhoc;
    adhoc.id = L"adhoc";
    adhoc.match.processName = exeName;
    adhoc.match.matchMode = MatchMode::Any;

    TargetSet result;
    MatchContext ctx;
    ctx.store = nullptr;
    ctx.adhocRule = &adhoc;
    ctx.out = &result;
    EnumWindows(&EnumTopLevelProc, reinterpret_cast<LPARAM>(&ctx));
    return result;
}

std::vector<WindowRef> ProcessWindowResolver::EnumerateVisibleTopLevelWindows() const {
    std::vector<WindowRef> result;
    EnumWindows(&EnumVisibleProc, reinterpret_cast<LPARAM>(&result));
    return result;
}

std::vector<HWND> ProcessWindowResolver::CollectWindowsForPid(DWORD pid) const {
    std::vector<HWND> result;
    if (pid == 0) return result;

    PidCollectContext ctx;
    ctx.pid = pid;
    ctx.out = &result;

    EnumWindows(&EnumPidProc, reinterpret_cast<LPARAM>(&ctx));

    // Include child windows owned by the same process.
    const std::vector<HWND> tops = result;
    for (HWND hwnd : tops) {
        EnumChildWindows(hwnd, &EnumChildProc, reinterpret_cast<LPARAM>(&ctx));
    }
    return result;
}

std::vector<HWND> ProcessWindowResolver::CollectOwnedPopups(HWND hwnd) const {
    std::vector<HWND> result;
    if (hwnd == nullptr) return result;
    OwnerContext ctx;
    ctx.owner = hwnd;
    ctx.out = &result;
    EnumWindows(&EnumOwnerProc, reinterpret_cast<LPARAM>(&ctx));
    return result;
}

}  // namespace core
}  // namespace hideutil
