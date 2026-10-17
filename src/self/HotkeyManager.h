// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

// RegisterHotKey / UnregisterHotKey wrapper with conflict detection.
#pragma once

#include "core/Types.h"

#include <windows.h>

#include <string>
#include <vector>

namespace hideutil {
namespace self {

class HotkeyManager {
public:
    static constexpr int kHotkeyHideToggle = 1;
    static constexpr int kHotkeyOpenSettings = 2;

    HotkeyManager();
    ~HotkeyManager();

    bool Register(HWND hwnd, int id, const core::HotkeySpec& spec);

    // Replace an existing hotkey atomically: the new combination is registered
    // first; on failure the previous registration is restored so we never end up
    // with "neither key works".
    bool ReRegister(HWND hwnd, int id, const core::HotkeySpec& spec);

    void Unregister(HWND hwnd, int id);
    void UnregisterAll(HWND hwnd);

    bool IsRegistered(int id) const;
    bool IsConflict(int id) const;   // failed with ERROR_HOTKEY_ALREADY_REGISTERED
    DWORD LastError(int id) const;

    static UINT ModifierFlag(const std::wstring& name);
    static bool KeyNameToVk(const std::wstring& name, UINT& vk);
    static bool Resolve(const core::HotkeySpec& spec, UINT& modFlags, UINT& vk);

private:
    struct Entry {
        int id = 0;
        UINT mod = 0;
        UINT vk = 0;
        bool registered = false;
        DWORD error = 0;
    };

    Entry* Find(int id);
    const Entry* Find(int id) const;

    std::vector<Entry> entries_;
};

}  // namespace self
}  // namespace hideutil
