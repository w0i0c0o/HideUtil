// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

// Best-effort tray icon hiding for the Windows 10 classic notification area
// (ToolbarWindow32). On Windows 11 there is no ToolbarWindow32; we degrade
// gracefully by returning false and never perform registry / explorer hacks.
#pragma once

#include "core/Types.h"

#include <windows.h>

#include <functional>
#include <vector>

namespace hideutil {
namespace core {

class TrayIconHider {
public:
    // Hide every tray icon whose owner process matches pid. Returns true when
    // at least one icon was hidden; false when unsupported or nothing matched.
    bool ApplyHide(DWORD pid);

    // Re-show tray icons previously hidden for pid.
    bool Restore(DWORD pid);

    // True when the classic tray toolbar can be located.
    static bool IsSupported();

    // Collect the PIDs that currently own a notification-area icon, without
    // changing any state. Returns an empty list on Windows 11 (XAML tray) or
    // whenever the classic toolbar cannot be read. Read-only: safe to call from
    // the settings UI to build a "pick a program" list.
    static std::vector<DWORD> EnumerateTrayOwnerPids();

private:
    struct HiddenIcon {
        DWORD pid = 0;
        HWND toolbar = nullptr;
        int commandId = 0;
        int index = -1;
    };

    static HWND FindTrayToolbar();

    // Walk every button of the classic tray toolbar, invoking `fn` with
    // (buttonIndex, ownerHwnd, ownerPid, commandId). Returning false from `fn`
    // stops the walk. Returns false when the tray is unsupported or unreadable.
    static bool ForEachTrayButton(
        const std::function<bool(int index, HWND ownerHwnd, DWORD ownerPid, int commandId)>& fn);

    std::vector<HiddenIcon> hidden_;
};

}  // namespace core
}  // namespace hideutil
