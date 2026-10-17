// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

#include "i18n/Localizer.h"

#include "core/Json.h"
#include "ui/AppResources.h"

#include <windows.h>

#include <map>
#include <string>
#include <vector>

namespace hideutil {
namespace i18n {

namespace {

using StringMap = std::map<std::wstring, std::wstring>;

// Process-wide localization state. The UI is single-threaded; the only other
// caller is the single-instance handoff on the process' main thread, so a plain
// file-scope state is sufficient. g_keyEcho is thread_local purely so a
// "missing key" echo never races with itself.
std::wstring g_lang = kLangEnUS;
StringMap g_current;
StringMap g_en;
std::wstring g_fileDir;
thread_local std::wstring g_keyEcho;

std::wstring Utf8ToWide(const char* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return std::wstring();
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(size),
                                           nullptr, 0);
    if (needed <= 0) {
        return std::wstring();
    }
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(size), &wide[0], needed);
    return wide;
}

std::wstring NarrowToWide(const char* text) {
    if (text == nullptr) {
        return std::wstring();
    }
    std::size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return Utf8ToWide(text, length);
}

bool ParseDocument(const std::wstring& text,
                   std::vector<std::pair<std::wstring, std::wstring>>& out) {
    core::JsonValue root;
    std::wstring error;
    if (!core::JsonValue::Parse(text, root, error) || !root.IsObject()) {
        return false;
    }
    for (const auto& entry : root.AsObject()) {
        if (entry.second.IsString()) {
            out.emplace_back(entry.first, entry.second.AsString());
        }
    }
    return true;
}

bool LoadFromResource(int resourceId,
                      std::vector<std::pair<std::wstring, std::wstring>>& out) {
    const HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return false;
    }
    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (resource == nullptr) {
        return false;
    }
    const HGLOBAL handle = LoadResource(module, resource);
    if (handle == nullptr) {
        return false;
    }
    const void* data = LockResource(handle);
    const DWORD size = SizeofResource(module, resource);
    if (data == nullptr || size == 0) {
        return false;
    }
    return ParseDocument(Utf8ToWide(static_cast<const char*>(data), size), out);
}

}  // namespace

void Localizer::SetFileDir(const std::wstring& dir) { g_fileDir = dir; }

const std::wstring& Localizer::Language() { return g_lang; }

std::wstring Localizer::CodeFromLangId(LANGID langId) {
    if (PRIMARYLANGID(langId) == LANG_CHINESE) {
        return kLangZhCN;  // every Chinese sublang maps to the one Chinese build
    }
    return kLangEnUS;  // English and every undeveloped language -> English
}

bool Localizer::LoadMapFromFile(const std::wstring& path,
                                std::vector<std::pair<std::wstring, std::wstring>>& out) {
    std::wstring text;
    if (!core::ReadTextFileUtf8(path, text)) {
        return false;
    }
    return ParseDocument(text, out);
}

bool Localizer::LoadStrings(const std::wstring& code,
                            std::vector<std::pair<std::wstring, std::wstring>>& out) {
    const int resourceId = (code == kLangZhCN) ? IDR_LANG_ZH_CN : IDR_LANG_EN_US;
    if (LoadFromResource(resourceId, out)) {
        return true;
    }
    if (!g_fileDir.empty()) {
        return LoadMapFromFile(g_fileDir + L"\\" + code + L".json", out);
    }
    return false;
}

void Localizer::Initialize(const std::wstring& overrideCode) {
    std::wstring code = overrideCode;
    if (code.empty() || code == kLangAuto) {
        code = CodeFromLangId(GetUserDefaultUILanguage());
    }
    if (code != kLangEnUS && code != kLangZhCN) {
        code = kLangEnUS;  // undeveloped language -> English default
    }

    g_en.clear();
    g_current.clear();

    std::vector<std::pair<std::wstring, std::wstring>> enPairs;
    LoadStrings(kLangEnUS, enPairs);
    for (const auto& pair : enPairs) {
        g_en[pair.first] = pair.second;
    }

    if (code == kLangEnUS) {
        g_current = g_en;
    } else {
        std::vector<std::pair<std::wstring, std::wstring>> curPairs;
        LoadStrings(code, curPairs);
        for (const auto& pair : curPairs) {
            g_current[pair.first] = pair.second;
        }
        // Any key the active language is missing falls through to g_en in Get().
    }
    g_lang = code;
}

const std::wstring& Localizer::Get(const std::wstring& key) {
    const auto it = g_current.find(key);
    if (it != g_current.end()) {
        return it->second;
    }
    const auto fallback = g_en.find(key);
    if (fallback != g_en.end()) {
        return fallback->second;
    }
    g_keyEcho = key;  // visible during testing rather than silently blank
    return g_keyEcho;
}

const std::wstring& Localizer::Get(const char* key) {
    const std::wstring wide = NarrowToWide(key);
    return Get(wide);
}

void Localizer::ReplaceAll(std::wstring& text, const std::wstring& from,
                           const std::wstring& to) {
    if (from.empty()) {
        return;
    }
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::wstring Localizer::FormatImpl(const std::wstring& templ,
                                   const std::vector<std::wstring>& args) {
    std::wstring result = templ;
    for (std::size_t i = 0; i < args.size(); ++i) {
        ReplaceAll(result, L"{" + std::to_wstring(i) + L"}", args[i]);
    }
    return result;
}

std::wstring Localizer::Format(const std::wstring& templ, const std::wstring& a0) {
    return FormatImpl(templ, {a0});
}

std::wstring Localizer::Format(const std::wstring& templ, const std::wstring& a0,
                               const std::wstring& a1) {
    return FormatImpl(templ, {a0, a1});
}

std::wstring Localizer::Format(const std::wstring& templ, const std::wstring& a0,
                               const std::wstring& a1, const std::wstring& a2) {
    return FormatImpl(templ, {a0, a1, a2});
}

std::wstring Localizer::Format(const std::wstring& templ, const std::wstring& a0,
                               const std::wstring& a1, const std::wstring& a2,
                               const std::wstring& a3) {
    return FormatImpl(templ, {a0, a1, a2, a3});
}

}  // namespace i18n
}  // namespace hideutil
