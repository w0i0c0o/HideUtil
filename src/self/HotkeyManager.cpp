// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

#include "self/HotkeyManager.h"

#include <cwchar>
#include <cwctype>

namespace hideutil {
namespace self {

namespace {

bool IEquals(const std::wstring& a, const wchar_t* b) {
    return _wcsicmp(a.c_str(), b) == 0;
}

}  // namespace

HotkeyManager::HotkeyManager() = default;
HotkeyManager::~HotkeyManager() = default;

UINT HotkeyManager::ModifierFlag(const std::wstring& name) {
    if (IEquals(name, L"CTRL") || IEquals(name, L"CONTROL")) return MOD_CONTROL;
    if (IEquals(name, L"ALT")) return MOD_ALT;
    if (IEquals(name, L"SHIFT")) return MOD_SHIFT;
    if (IEquals(name, L"WIN") || IEquals(name, L"WINDOWS")) return MOD_WIN;
    return 0;
}

bool HotkeyManager::KeyNameToVk(const std::wstring& name, UINT& vk) {
    if (name.empty()) {
        return false;
    }
    if (name.size() == 1) {
        const wchar_t c = static_cast<wchar_t>(std::towupper(name[0]));
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) {
            vk = static_cast<UINT>(c);
            return true;
        }
    }
    if ((name[0] == L'F' || name[0] == L'f') && name.size() >= 2) {
        const int index = _wtoi(name.c_str() + 1);
        if (index >= 1 && index <= 24) {
            vk = static_cast<UINT>(VK_F1 + (index - 1));
            return true;
        }
    }
    // "VK_0x7B" form.
    if (_wcsnicmp(name.c_str(), L"VK_0x", 5) == 0) {
        const long value = wcstol(name.c_str() + 5, nullptr, 16);
        if (value > 0 && value <= 0xFF) {
            vk = static_cast<UINT>(value);
            return true;
        }
        return false;
    }
    struct NamedKey {
        const wchar_t* name;
        UINT vk;
    };
    static const NamedKey kNamed[] = {
        {L"SPACE", VK_SPACE},   {L"TAB", VK_TAB},       {L"ESC", VK_ESCAPE},
        {L"ESCAPE", VK_ESCAPE}, {L"ENTER", VK_RETURN},  {L"RETURN", VK_RETURN},
        {L"BACKSPACE", VK_BACK},{L"DELETE", VK_DELETE}, {L"INSERT", VK_INSERT},
        {L"HOME", VK_HOME},     {L"END", VK_END},       {L"PGUP", VK_PRIOR},
        {L"PAGEUP", VK_PRIOR},  {L"PGDN", VK_NEXT},     {L"PAGEDOWN", VK_NEXT},
        {L"UP", VK_UP},         {L"DOWN", VK_DOWN},     {L"LEFT", VK_LEFT},
        {L"RIGHT", VK_RIGHT},   {L"PAUSE", VK_PAUSE},   {L"PRINTSCREEN", VK_SNAPSHOT},
        {L"CAPSLOCK", VK_CAPITAL},
    };
    for (const NamedKey& key : kNamed) {
        if (IEquals(name, key.name)) {
            vk = key.vk;
            return true;
        }
    }
    return false;
}

bool HotkeyManager::Resolve(const core::HotkeySpec& spec, UINT& modFlags, UINT& vk) {
    UINT mods = 0;
    for (const std::wstring& modifier : spec.modifiers) {
        mods |= ModifierFlag(modifier);
    }
    UINT code = 0;
    if (!KeyNameToVk(spec.key, code)) {
        return false;
    }
    modFlags = mods;
    vk = code;
    return true;
}

HotkeyManager::Entry* HotkeyManager::Find(int id) {
    for (Entry& entry : entries_) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

const HotkeyManager::Entry* HotkeyManager::Find(int id) const {
    for (const Entry& entry : entries_) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

bool HotkeyManager::Register(HWND hwnd, int id, const core::HotkeySpec& spec) {
    Entry entry;
    entry.id = id;

    UINT mod = 0;
    UINT vk = 0;
    if (!Resolve(spec, mod, vk)) {
        entry.error = ERROR_INVALID_PARAMETER;
        if (Entry* existing = Find(id)) {
            *existing = entry;
        } else {
            entries_.push_back(entry);
        }
        return false;
    }
    entry.mod = mod;
    entry.vk = vk;

    if (!RegisterHotKey(hwnd, id, mod | MOD_NOREPEAT, vk)) {
        entry.error = GetLastError();
        entry.registered = false;
    } else {
        entry.registered = true;
        entry.error = 0;
    }

    if (Entry* existing = Find(id)) {
        *existing = entry;
    } else {
        entries_.push_back(entry);
    }
    return entry.registered;
}

bool HotkeyManager::ReRegister(HWND hwnd, int id, const core::HotkeySpec& spec) {
    UINT newMod = 0;
    UINT newVk = 0;
    if (!Resolve(spec, newMod, newVk)) {
        return false;  // invalid spec: leave the existing registration untouched
    }

    Entry* existing = Find(id);
    const bool hadOld = (existing != nullptr && existing->registered);
    const UINT oldMod = existing != nullptr ? existing->mod : 0;
    const UINT oldVk = existing != nullptr ? existing->vk : 0;

    // Release the previous binding before probing the new one.
    if (hadOld) {
        UnregisterHotKey(hwnd, id);
    }

    if (!RegisterHotKey(hwnd, id, newMod | MOD_NOREPEAT, newVk)) {
        const DWORD error = GetLastError();
        // Roll back to the previous combination so we never leave a gap.
        bool rolledBack = false;
        if (hadOld) {
            rolledBack = RegisterHotKey(hwnd, id, oldMod | MOD_NOREPEAT, oldVk) != FALSE;
        }
        Entry entry;
        entry.id = id;
        entry.mod = hadOld ? oldMod : 0;
        entry.vk = hadOld ? oldVk : 0;
        entry.registered = rolledBack;
        entry.error = error;
        if (existing != nullptr) {
            *existing = entry;
        } else {
            entries_.push_back(entry);
        }
        return false;
    }

    Entry entry;
    entry.id = id;
    entry.mod = newMod;
    entry.vk = newVk;
    entry.registered = true;
    entry.error = 0;
    if (existing != nullptr) {
        *existing = entry;
    } else {
        entries_.push_back(entry);
    }
    return true;
}

void HotkeyManager::Unregister(HWND hwnd, int id) {
    Entry* entry = Find(id);
    if (entry != nullptr && entry->registered) {
        UnregisterHotKey(hwnd, id);
        entry->registered = false;
    }
}

void HotkeyManager::UnregisterAll(HWND hwnd) {
    for (Entry& entry : entries_) {
        if (entry.registered) {
            UnregisterHotKey(hwnd, entry.id);
            entry.registered = false;
        }
    }
}

bool HotkeyManager::IsRegistered(int id) const {
    const Entry* entry = Find(id);
    return entry != nullptr && entry->registered;
}

bool HotkeyManager::IsConflict(int id) const {
    const Entry* entry = Find(id);
    return entry != nullptr && !entry->registered && entry->error == ERROR_HOTKEY_ALREADY_REGISTERED;
}

DWORD HotkeyManager::LastError(int id) const {
    const Entry* entry = Find(id);
    return entry != nullptr ? entry->error : ERROR_SUCCESS;
}

}  // namespace self
}  // namespace hideutil
