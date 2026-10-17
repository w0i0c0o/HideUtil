// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

// Central PID <-> HWND aggregation. No other module enumerates windows
// directly (design document section 10: "PID <-> HWND mapping").
#pragma once

#include "core/AppRuleStore.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace hideutil {
namespace core {

class ProcessWindowResolver {
public:
    ProcessWindowResolver() = default;

    // Resolve every window / PID that matches at least one enabled rule.
    TargetSet ResolveTargets(const AppRuleStore& store) const;

    // Resolve windows for an image name (e.g. "notepad.exe"); used by the CLI
    // and by ad-hoc testing. Creates a transient rule internally.
    TargetSet ResolveByExeName(const std::wstring& exeName) const;

    // Every visible top-level window (used by the CLI `--list` command).
    std::vector<WindowRef> EnumerateVisibleTopLevelWindows() const;

    // All top-level / owned / child windows belonging to a PID.
    std::vector<HWND> CollectWindowsForPid(DWORD pid) const;

    // Top-level windows owned by the given window.
    std::vector<HWND> CollectOwnedPopups(HWND hwnd) const;

    // True when the window class is "ApplicationFrameWindow" (UWP host).
    static bool IsUwpFrame(HWND hwnd);

    static std::wstring GetWindowClass(HWND hwnd);
    static std::wstring GetProcessImagePath(DWORD pid);
    static std::wstring GetProcessBaseName(const std::wstring& path);
};

}  // namespace core
}  // namespace hideutil
