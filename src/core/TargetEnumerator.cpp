#include "core/TargetEnumerator.h"

#include "core/TrayIconHider.h"

#include <tlhelp32.h>

#include <algorithm>
#include <cwctype>
#include <iterator>
#include <map>

namespace hideutil {
namespace core {

namespace {

std::wstring WindowText(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return std::wstring();
    }
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(hwnd, text.data(), length + 1);
    if (copied <= 0) {
        return std::wstring();
    }
    text.resize(static_cast<std::size_t>(copied));
    return text;
}

// Executables that have registered a notification-area icon, read from the
// per-user notify-icon list:
//   HKCU\Control Panel\NotifyIconSettings\{GUID}\ExecutablePath
//
// Windows 11 draws the notification area with XAML, so the classic toolbar
// enumeration in TrayIconHider cannot see any icon there. This registry list is
// the only available way to tell which programs are tray programs.
//
// READ-ONLY: the key is opened with KEY_READ and is never created or written.
// HideUtil deliberately does NOT use this key to hide anything.
std::vector<std::wstring> ReadRegisteredTrayExecutables() {
    std::vector<std::wstring> paths;
    HKEY settings = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\NotifyIconSettings", 0, KEY_READ,
                      &settings) != ERROR_SUCCESS) {
        return paths;
    }

    wchar_t subName[512];
    for (DWORD index = 0;; ++index) {
        DWORD subNameLength = static_cast<DWORD>(std::size(subName));
        const LSTATUS enumStatus =
            RegEnumKeyExW(settings, index, subName, &subNameLength, nullptr, nullptr, nullptr, nullptr);
        if (enumStatus != ERROR_SUCCESS) {
            break;  // ERROR_NO_MORE_ITEMS or a real failure: either way we stop.
        }
        HKEY entry = nullptr;
        if (RegOpenKeyExW(settings, subName, 0, KEY_READ, &entry) != ERROR_SUCCESS) {
            continue;
        }
        wchar_t buffer[2048];
        DWORD size = sizeof(buffer);
        DWORD type = 0;
        if (RegQueryValueExW(entry, L"ExecutablePath", nullptr, &type,
                             reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS &&
            type == REG_SZ) {
            std::wstring path(buffer, size / sizeof(wchar_t));
            while (!path.empty() && path.back() == L'\0') {
                path.pop_back();
            }
            if (!path.empty()) {
                paths.push_back(std::move(path));
            }
        }
        RegCloseKey(entry);
    }
    RegCloseKey(settings);
    return paths;
}

// Best-effort elevation probe. A process we cannot even query is reported as
// likely elevated (or protected), which is exactly the case where hiding will
// fail unless HideUtil itself runs elevated.
bool DetectElevated(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return true;
    }
    bool elevated = false;
    HANDLE token = nullptr;
    if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION info{};
        DWORD size = sizeof(info);
        if (GetTokenInformation(token, TokenElevation, &info, sizeof(info), &size)) {
            elevated = info.TokenIsElevated != 0;
        }
        CloseHandle(token);
    } else if (GetLastError() == ERROR_ACCESS_DENIED) {
        elevated = true;
    }
    CloseHandle(process);
    return elevated;
}

void FillIdentity(TargetCandidate& candidate) {
    if (candidate.exePath.empty()) {
        candidate.exePath = ProcessWindowResolver::GetProcessImagePath(candidate.pid);
    }
    if (candidate.processName.empty()) {
        candidate.processName = ProcessWindowResolver::GetProcessBaseName(candidate.exePath);
    }
}

std::vector<TargetCandidate> CollectProcesses() {
    std::vector<TargetCandidate> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return result;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == 0 || entry.th32ProcessID == 4) {
                continue;  // System Idle / System
            }
            TargetCandidate candidate;
            candidate.pid = entry.th32ProcessID;
            candidate.processName = entry.szExeFile;
            candidate.exePath = ProcessWindowResolver::GetProcessImagePath(candidate.pid);
            if (candidate.processName.empty()) {
                candidate.processName = ProcessWindowResolver::GetProcessBaseName(candidate.exePath);
            }
            result.push_back(std::move(candidate));
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

}  // namespace

std::wstring ToLowerCopy(const std::wstring& text) {
    std::wstring lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
    });
    return lowered;
}

std::vector<TargetCandidate> MergeCandidates(std::vector<TargetCandidate> windowBased,
                                             std::vector<TargetCandidate> trayBased,
                                             std::vector<TargetCandidate> processBased,
                                             DWORD selfPid) {
    std::map<DWORD, TargetCandidate> merged;
    auto ensure = [&merged](DWORD pid) -> TargetCandidate& {
        TargetCandidate& slot = merged[pid];
        slot.pid = pid;
        return slot;
    };
    auto fillNames = [](TargetCandidate& target, const TargetCandidate& source) {
        if (target.processName.empty()) target.processName = source.processName;
        if (target.exePath.empty()) target.exePath = source.exePath;
    };

    // Weakest source first: later, richer sources only fill what is missing.
    for (const TargetCandidate& candidate : processBased) {
        if (candidate.pid == 0) continue;
        fillNames(ensure(candidate.pid), candidate);
    }
    for (const TargetCandidate& candidate : trayBased) {
        if (candidate.pid == 0) continue;
        TargetCandidate& slot = ensure(candidate.pid);
        fillNames(slot, candidate);
        slot.hasTrayIcon = true;
    }
    for (const TargetCandidate& candidate : windowBased) {
        if (candidate.pid == 0) continue;
        TargetCandidate& slot = ensure(candidate.pid);
        fillNames(slot, candidate);
        slot.hasVisibleWindow = true;
        slot.isForeground = slot.isForeground || candidate.isForeground;
        if (slot.windowTitle.empty()) slot.windowTitle = candidate.windowTitle;
        if (slot.windowClass.empty()) slot.windowClass = candidate.windowClass;
    }

    std::vector<TargetCandidate> result;
    result.reserve(merged.size());
    for (auto& pair : merged) {
        // Never offer HideUtil itself as a target.
        if (pair.first == selfPid) continue;
        result.push_back(pair.second);
    }
    SortCandidates(result);
    return result;
}

void SortCandidates(std::vector<TargetCandidate>& candidates) {
    std::sort(candidates.begin(), candidates.end(),
              [](const TargetCandidate& a, const TargetCandidate& b) {
                  if (a.isForeground != b.isForeground) return a.isForeground;
                  if (a.hasVisibleWindow != b.hasVisibleWindow) return a.hasVisibleWindow;
                  if (a.hasTrayIcon != b.hasTrayIcon) return a.hasTrayIcon;
                  const int byName = _wcsicmp(a.processName.c_str(), b.processName.c_str());
                  if (byName != 0) return byName < 0;
                  return a.pid < b.pid;
              });
}

bool MatchesFilter(const TargetCandidate& candidate, const std::wstring& lowerQuery) {
    if (lowerQuery.empty()) {
        return true;
    }
    if (ToLowerCopy(candidate.processName).find(lowerQuery) != std::wstring::npos) return true;
    if (ToLowerCopy(candidate.windowTitle).find(lowerQuery) != std::wstring::npos) return true;
    if (ToLowerCopy(candidate.exePath).find(lowerQuery) != std::wstring::npos) return true;
    return false;
}

void MarkRegisteredTrayApps(std::vector<TargetCandidate>& candidates,
                            const std::vector<std::wstring>& registeredPaths) {
    if (registeredPaths.empty()) {
        return;
    }
    std::vector<std::wstring> lowered;
    lowered.reserve(registeredPaths.size());
    for (const std::wstring& path : registeredPaths) {
        lowered.push_back(ToLowerCopy(path));
    }
    for (TargetCandidate& candidate : candidates) {
        if (candidate.hasTrayIcon || candidate.exePath.empty()) {
            continue;
        }
        const std::wstring candidatePath = ToLowerCopy(candidate.exePath);
        for (const std::wstring& path : lowered) {
            if (candidatePath == path) {
                candidate.hasTrayIcon = true;
                break;
            }
        }
    }
}

std::wstring CandidateBadges(const TargetCandidate& candidate) {
    std::wstring badges;
    if (candidate.isForeground) badges += L"前台 ";
    if (candidate.hasVisibleWindow) badges += L"窗口 ";
    if (candidate.hasTrayIcon) badges += L"托盘 ";
    if (candidate.likelyElevated) badges += L"管理员 ";
    if (badges.empty()) badges = L"后台";
    while (!badges.empty() && badges.back() == L' ') {
        badges.pop_back();
    }
    return badges;
}

std::wstring DescribeCandidate(const TargetCandidate& candidate) {
    std::wstring result = L"[" + CandidateBadges(candidate) + L"] ";
    result += candidate.processName.empty() ? L"(未知程序)" : candidate.processName;
    if (!candidate.windowTitle.empty()) {
        result += L" - ";
        result += candidate.windowTitle;
    }
    return result;
}

std::vector<TargetCandidate> EnumerateTargetCandidates(const ProcessWindowResolver& resolver) {
    std::vector<TargetCandidate> windowBased;
    const HWND foreground = GetForegroundWindow();
    for (const WindowRef& ref : resolver.EnumerateVisibleTopLevelWindows()) {
        if (ref.pid == 0) continue;
        TargetCandidate candidate;
        candidate.pid = ref.pid;
        candidate.windowTitle = WindowText(ref.hwnd);
        candidate.windowClass = ProcessWindowResolver::GetWindowClass(ref.hwnd);
        candidate.hasVisibleWindow = true;
        candidate.isForeground = (ref.hwnd == foreground);
        FillIdentity(candidate);
        windowBased.push_back(std::move(candidate));
    }

    std::vector<TargetCandidate> trayBased;
    for (DWORD pid : TrayIconHider::EnumerateTrayOwnerPids()) {
        if (pid == 0) continue;
        TargetCandidate candidate;
        candidate.pid = pid;
        candidate.hasTrayIcon = true;
        FillIdentity(candidate);
        trayBased.push_back(std::move(candidate));
    }

    std::vector<TargetCandidate> merged =
        MergeCandidates(std::move(windowBased), std::move(trayBased), CollectProcesses(),
                        GetCurrentProcessId());

    // Windows 11 draws the notification area with XAML, so the live tray
    // enumeration yields nothing. Fill in tray programs from the per-user
    // notify-icon registry and re-sort so they surface near the top.
    MarkRegisteredTrayApps(merged, ReadRegisteredTrayExecutables());
    SortCandidates(merged);

    // Elevation probing is comparatively expensive, so only annotate the
    // programs a user is actually likely to pick.
    for (TargetCandidate& candidate : merged) {
        if (candidate.hasVisibleWindow || candidate.hasTrayIcon) {
            candidate.likelyElevated = DetectElevated(candidate.pid);
        }
    }
    return merged;
}

}  // namespace core
}  // namespace hideutil
