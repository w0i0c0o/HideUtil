// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

// Runtime localization for HideUtil.
//
// UI strings live in per-language JSON documents (resources/lang/*.json) that
// are embedded into the executable as RCDATA resources, so the deliverable
// stays a single portable file. At startup the effective language is resolved
// from the user's configuration ("auto" follows the system UI language) and the
// matching document is loaded; English (en-US) is always loaded as the fallback
// base.
//
// Two-level fallback, per the product requirement:
//   * language level: an unsupported / undeveloped language -> en-US;
//   * key level:      a key missing in the active language -> en-US, and a key
//                     missing there too -> the key itself (so a missing
//                     translation is visible in testing rather than silent).
//
// Every supported language document MUST carry the same key set; the unit test
// `TestLocalizerFallbackAndParity` enforces that (see tests/core_tests.cpp).
#pragma once

#include <windows.h>

#include <string>
#include <utility>
#include <vector>

namespace hideutil {
namespace i18n {

// Supported language codes. "auto" follows the system UI language.
constexpr const wchar_t* kLangAuto = L"auto";
constexpr const wchar_t* kLangEnUS = L"en-US";
constexpr const wchar_t* kLangZhCN = L"zh-CN";

class Localizer {
public:
    // Resolve the effective language and load its strings. Resource-based by
    // default; call SetFileDir() first to load from files (unit tests).
    // `overrideCode` is "auto", "en-US" or "zh-CN".
    static void Initialize(const std::wstring& overrideCode = kLangAuto);

    // Load strings from <dir>/<code>.json instead of the embedded resource.
    static void SetFileDir(const std::wstring& dir);

    // The language code actually in effect after Initialize().
    static const std::wstring& Language();

    // Translate a key (active language -> en-US -> the key itself).
    static const std::wstring& Get(const char* key);
    static const std::wstring& Get(const std::wstring& key);

    // Substitute positional placeholders {0}, {1}, ... into an already
    // translated template.
    static std::wstring Format(const std::wstring& templ, const std::wstring& a0);
    static std::wstring Format(const std::wstring& templ, const std::wstring& a0,
                               const std::wstring& a1);
    static std::wstring Format(const std::wstring& templ, const std::wstring& a0,
                               const std::wstring& a1, const std::wstring& a2);
    static std::wstring Format(const std::wstring& templ, const std::wstring& a0,
                               const std::wstring& a1, const std::wstring& a2,
                               const std::wstring& a3);

    // Map a Windows LANGID to a supported language code: any Chinese sublang
    // -> zh-CN, everything else -> en-US.
    static std::wstring CodeFromLangId(LANGID langId);

    // Exposed for the parity test: parse a language document into a flat
    // key -> value map. Returns false when the document cannot be read/parsed.
    static bool LoadMapFromFile(const std::wstring& path,
                                std::vector<std::pair<std::wstring, std::wstring>>& out);

private:
    Localizer() = delete;
    static std::wstring FormatImpl(const std::wstring& templ,
                                   const std::vector<std::wstring>& args);
    static bool LoadStrings(const std::wstring& code,
                            std::vector<std::pair<std::wstring, std::wstring>>& out);
    static void ReplaceAll(std::wstring& text, const std::wstring& from,
                           const std::wstring& to);
};

}  // namespace i18n
}  // namespace hideutil
