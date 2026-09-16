// Target discovery for the settings UI.
//
// Instead of asking the user to type an executable path by hand, we merge three
// live sources into one pickable list:
//   1. visible top-level windows (the program the user actually sees)
//   2. notification-area (tray) icon owners
//   3. every running process (so background/tray-only programs are reachable)
//
// The merge / filter / describe helpers are pure functions with no Win32 calls
// so they can be unit tested without a desktop session.
#pragma once

#include "core/ProcessWindowResolver.h"

#include <windows.h>

#include <string>
#include <vector>

namespace hideutil {
namespace core {

// One selectable target program.
struct TargetCandidate {
    DWORD pid = 0;
    std::wstring processName;   // e.g. L"notepad.exe"
    std::wstring exePath;       // full image path; may be empty when inaccessible
    std::wstring windowTitle;   // title of the primary visible top-level window
    std::wstring windowClass;   // class of that window
    bool hasVisibleWindow = false;
    bool hasTrayIcon = false;
    bool isForeground = false;

    // Best-effort: true when the process runs at a higher integrity level than
    // us, which means hiding it requires HideUtil itself to be elevated.
    bool likelyElevated = false;
};

// Live enumeration (Win32). Never throws; returns whatever could be read.
std::vector<TargetCandidate> EnumerateTargetCandidates(const ProcessWindowResolver& resolver);

// --- pure helpers (unit tested, no Win32 APIs) ------------------------------

// Merge the three sources keyed by PID: window data wins for title/class, the
// tray flag is OR-ed, `selfPid` is dropped, then sort by
// (foreground, hasVisibleWindow, hasTrayIcon, processName, pid).
std::vector<TargetCandidate> MergeCandidates(std::vector<TargetCandidate> windowBased,
                                             std::vector<TargetCandidate> trayBased,
                                             std::vector<TargetCandidate> processBased,
                                             DWORD selfPid);

// Case-insensitive substring match on process name / window title / exe path.
// `lowerQuery` must already be lower-cased (see ToLowerCopy).
bool MatchesFilter(const TargetCandidate& candidate, const std::wstring& lowerQuery);

// Mark every candidate whose executable matches one of `registeredPaths`
// (case-insensitive) as a tray program. Kept pure so it can be unit tested;
// used to combine the Windows 11 registry-derived tray list with the live
// process list.
void MarkRegisteredTrayApps(std::vector<TargetCandidate>& candidates,
                            const std::vector<std::wstring>& registeredPaths);

// Sort in place by (foreground, hasVisibleWindow, hasTrayIcon, name, pid) so
// the programs a user is most likely to want appear first.
void SortCandidates(std::vector<TargetCandidate>& candidates);

// Lower-cased copy, for feeding MatchesFilter.
std::wstring ToLowerCopy(const std::wstring& text);

// One-line description for the picker list, e.g.
// "[前台] [窗口] notepad.exe - 无标题 - Notepad".
std::wstring DescribeCandidate(const TargetCandidate& candidate);

// Compact badge column used by the picker list, e.g. L"前台 窗口 托盘".
std::wstring CandidateBadges(const TargetCandidate& candidate);

}  // namespace core
}  // namespace hideutil
