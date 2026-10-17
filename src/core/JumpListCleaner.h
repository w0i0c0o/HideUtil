// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

// Per-application jump list / destination cleanup.
//
// IMPORTANT: this deliberately does NOT call SHAddToRecentDocs(SHARD_PATHW,
// nullptr), which wipes the GLOBAL recent-documents list and would damage other
// applications' history. Instead we use IApplicationDestinations bound to the
// target application's AppUserModelID (per-app). When the app has no AppID we
// return false and report "unsupported".
#pragma once

#include <windows.h>

#include <string>

namespace hideutil {
namespace core {

class JumpListCleaner {
public:
    // Clear the recent/frequent destinations of the application owning pid.
    bool ClearForProcess(DWORD pid);

    // Clear destinations for an explicit AppUserModelID.
    bool ClearForAppId(const std::wstring& appUserModelId);

    // Resolve a process' explicit AppUserModelID; false when none is set.
    static bool TryGetAppUserModelId(DWORD pid, std::wstring& appId);
};

}  // namespace core
}  // namespace hideutil
