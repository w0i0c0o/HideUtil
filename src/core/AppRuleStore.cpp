// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

#include "core/AppRuleStore.h"

#include "core/Json.h"
#include "core/ProcessWindowResolver.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>

namespace hideutil {
namespace core {

namespace {

bool IEquals(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    }
    return true;
}

bool Contains(const std::vector<std::wstring>& ids, const std::wstring& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// Smallest positive integer N such that "rule-N" is not in `taken`. With N
// taken ids the answer is at most N + 1, so the bounded loop always hits.
std::wstring FirstFreeRuleId(const std::vector<std::wstring>& taken) {
    for (std::size_t n = 1; n <= taken.size() + 1; ++n) {
        const std::wstring candidate = L"rule-" + std::to_wstring(n);
        if (!Contains(taken, candidate)) {
            return candidate;
        }
    }
    return L"rule-" + std::to_wstring(taken.size() + 1);
}

std::wstring ReadOptionalString(const JsonValue* value) {
    if (!value || value->IsNull()) return std::wstring();
    return value->IsString() ? value->AsString() : std::wstring();
}

std::vector<std::wstring> ReadStringArray(const JsonValue* value) {
    std::vector<std::wstring> result;
    if (!value || !value->IsArray()) return result;
    for (std::size_t i = 0; i < value->Size(); ++i) {
        if ((*value)[i].IsString()) {
            result.push_back((*value)[i].AsString());
        }
    }
    return result;
}

bool ReadBool(const JsonValue* value, bool fallback) {
    return (value && value->IsBool()) ? value->AsBool() : fallback;
}

HotkeySpec ParseHotkeySpec(const JsonValue* value) {
    HotkeySpec spec;
    if (!value || !value->IsObject()) return spec;
    spec.modifiers = ReadStringArray(value->Find(L"modifiers"));
    spec.key = ReadOptionalString(value->Find(L"key"));
    return spec;
}

JsonValue HotkeySpecToJson(const HotkeySpec& spec) {
    JsonValue obj = JsonValue::MakeObject();
    JsonValue mods = JsonValue::MakeArray();
    for (const auto& m : spec.modifiers) {
        mods.Push(JsonValue::MakeString(m));
    }
    obj.Set(L"modifiers", std::move(mods));
    obj.Set(L"key", JsonValue::MakeString(spec.key));
    return obj;
}

JsonValue RuleToJson(const AppRule& rule) {
    JsonValue obj = JsonValue::MakeObject();
    obj.Set(L"id", JsonValue::MakeString(rule.id));
    obj.Set(L"enabled", JsonValue::MakeBool(rule.enabled));
    obj.Set(L"name", JsonValue::MakeString(rule.name));

    JsonValue match = JsonValue::MakeObject();
    match.Set(L"exePath", rule.match.exePath ? JsonValue::MakeString(*rule.match.exePath)
                                             : JsonValue::MakeNull());
    match.Set(L"processName", rule.match.processName ? JsonValue::MakeString(*rule.match.processName)
                                                     : JsonValue::MakeNull());
    match.Set(L"windowClass", rule.match.windowClass ? JsonValue::MakeString(*rule.match.windowClass)
                                                     : JsonValue::MakeNull());
    match.Set(L"matchMode", JsonValue::MakeString(
        rule.match.matchMode == MatchMode::All ? L"all" : L"any"));
    obj.Set(L"match", std::move(match));

    JsonValue actions = JsonValue::MakeObject();
    actions.Set(L"hideWindow", JsonValue::MakeBool(rule.actions.hideWindow));
    actions.Set(L"hideTrayIcon", JsonValue::MakeBool(rule.actions.hideTrayIcon));
    actions.Set(L"muteAudio", JsonValue::MakeBool(rule.actions.muteAudio));
    actions.Set(L"suspendProcess", JsonValue::MakeBool(rule.actions.suspendProcess));
    actions.Set(L"moveToVirtualDesktop", JsonValue::MakeBool(rule.actions.moveToVirtualDesktop));
    actions.Set(L"hideFromAltTab", JsonValue::MakeBool(rule.actions.hideFromAltTab));
    actions.Set(L"clearJumpList", JsonValue::MakeBool(rule.actions.clearJumpList));
    obj.Set(L"actions", std::move(actions));

    JsonValue restore = JsonValue::MakeObject();
    restore.Set(L"restoreWindowPosition", JsonValue::MakeBool(rule.restore.restoreWindowPosition));
    restore.Set(L"restoreForegroundWindow", JsonValue::MakeBool(rule.restore.restoreForegroundWindow));
    obj.Set(L"restore", std::move(restore));
    return obj;
}

AppRule RuleFromJson(const JsonValue& value) {
    AppRule rule;
    rule.id = ReadOptionalString(value.Find(L"id"));
    rule.enabled = ReadBool(value.Find(L"enabled"), true);
    rule.name = ReadOptionalString(value.Find(L"name"));

    if (const JsonValue* match = value.Find(L"match")) {
        const std::wstring exePath = ReadOptionalString(match->Find(L"exePath"));
        const std::wstring processName = ReadOptionalString(match->Find(L"processName"));
        const std::wstring windowClass = ReadOptionalString(match->Find(L"windowClass"));
        if (!exePath.empty()) rule.match.exePath = exePath;
        if (!processName.empty()) rule.match.processName = processName;
        if (!windowClass.empty()) rule.match.windowClass = windowClass;
        const std::wstring mode = ReadOptionalString(match->Find(L"matchMode"));
        rule.match.matchMode = IEquals(mode, L"all") ? MatchMode::All : MatchMode::Any;
    }

    if (const JsonValue* actions = value.Find(L"actions")) {
        rule.actions.hideWindow = ReadBool(actions->Find(L"hideWindow"), rule.actions.hideWindow);
        rule.actions.hideTrayIcon = ReadBool(actions->Find(L"hideTrayIcon"), rule.actions.hideTrayIcon);
        rule.actions.muteAudio = ReadBool(actions->Find(L"muteAudio"), rule.actions.muteAudio);
        rule.actions.suspendProcess = ReadBool(actions->Find(L"suspendProcess"), rule.actions.suspendProcess);
        rule.actions.moveToVirtualDesktop = ReadBool(actions->Find(L"moveToVirtualDesktop"), rule.actions.moveToVirtualDesktop);
        rule.actions.hideFromAltTab = ReadBool(actions->Find(L"hideFromAltTab"), rule.actions.hideFromAltTab);
        rule.actions.clearJumpList = ReadBool(actions->Find(L"clearJumpList"), rule.actions.clearJumpList);
    }

    if (const JsonValue* restore = value.Find(L"restore")) {
        rule.restore.restoreWindowPosition = ReadBool(restore->Find(L"restoreWindowPosition"),
                                                      rule.restore.restoreWindowPosition);
        rule.restore.restoreForegroundWindow = ReadBool(restore->Find(L"restoreForegroundWindow"),
                                                        rule.restore.restoreForegroundWindow);
    }
    return rule;
}

}  // namespace

AppRuleStore::AppRuleStore() {
    config_ = MakeDefaultConfig();
}

std::wstring AppRuleStore::DefaultConfigPath() {
    PWSTR appData = nullptr;
    std::wstring base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)) && appData) {
        base.assign(appData);
        CoTaskMemFree(appData);
    } else {
        wchar_t buffer[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH) > 0) {
            base.assign(buffer);
        }
    }
    if (base.empty()) {
        return L"config.json";
    }
    return base + L"\\HideUtil\\config.json";
}

bool AppRuleStore::Load(const std::wstring& path) {
    std::wstring text;
    if (!ReadTextFileUtf8(path, text)) {
        return false;
    }
    JsonValue root;
    std::wstring error;
    if (!JsonValue::Parse(text, root, error)) {
        return false;
    }
    AppConfig parsed;
    if (!JsonToConfig(root, parsed, error)) {
        return false;
    }
    config_ = parsed;
    return true;
}

bool AppRuleStore::LoadDefault() {
    return Load(DefaultConfigPath());
}

bool AppRuleStore::Save(const std::wstring& path) const {
    const JsonValue root = ConfigToJson(config_);
    return WriteTextFileUtf8(path, root.Dump(2));
}

bool AppRuleStore::EnsureDefaultConfig(const std::wstring& path) {
    // Make sure the parent directory exists.
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        const std::wstring dir = path.substr(0, slash);
        CreateDirectoryW(dir.c_str(), nullptr);
    }
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    config_ = MakeDefaultConfig();
    return Save(path);
}

const AppRule* AppRuleStore::Match(const std::wstring& exePath,
                                   const std::wstring& processName,
                                   const std::wstring& windowClass) const {
    for (const auto& rule : config_.rules) {
        if (!rule.enabled) continue;
        if (RuleMatches(rule, exePath, processName, windowClass)) {
            return &rule;
        }
    }
    return nullptr;
}

const AppRule* AppRuleStore::MatchByPid(DWORD pid) const {
    const std::wstring exePath = ProcessWindowResolver::GetProcessImagePath(pid);
    return Match(exePath, ProcessWindowResolver::GetProcessBaseName(exePath), std::wstring());
}

const AppRule* AppRuleStore::MatchByExePath(const std::wstring& exePath) const {
    return Match(exePath, ProcessWindowResolver::GetProcessBaseName(exePath), std::wstring());
}

bool AppRuleStore::RuleMatches(const AppRule& rule,
                               const std::wstring& exePath,
                               const std::wstring& processName,
                               const std::wstring& windowClass) {
    const bool hasExe = rule.match.exePath.has_value() && !rule.match.exePath->empty();
    const bool hasProc = rule.match.processName.has_value() && !rule.match.processName->empty();
    const bool hasClass = rule.match.windowClass.has_value() && !rule.match.windowClass->empty();

    if (!hasExe && !hasProc && !hasClass) {
        return false;  // a rule with no criteria never matches
    }

    const bool exeHit = hasExe && IEquals(exePath, *rule.match.exePath);
    const bool procHit = hasProc && IEquals(processName, *rule.match.processName);
    const bool classHit = hasClass && IEquals(windowClass, *rule.match.windowClass);

    if (rule.match.matchMode == MatchMode::All) {
        if (hasExe && !exeHit) return false;
        if (hasProc && !procHit) return false;
        if (hasClass && !classHit) return false;
        return true;
    }
    // MatchMode::Any
    return exeHit || procHit || classHit;
}

AppConfig MakeDefaultConfig() {
    AppConfig config;
    config.version = 1;

    config.hotkey.hideToggle.modifiers = {L"CTRL", L"ALT"};
    config.hotkey.hideToggle.key = L"F12";

    config.hotkey.openSettings.modifiers = {L"CTRL", L"ALT"};
    config.hotkey.openSettings.key = L"F11";

    config.runtime.requireElevation = true;
    config.runtime.startHidden = true;
    config.runtime.registerAutostart = false;
    config.runtime.pollIntervalMs = 1500;
    config.language = L"auto";
    return config;
}

JsonValue ConfigToJson(const AppConfig& config) {
    JsonValue root = JsonValue::MakeObject();
    root.Set(L"$schema", JsonValue::MakeString(L"https://example.com/hideutil/config.schema.json"));
    root.Set(L"version", JsonValue::MakeNumber(config.version));
    root.Set(L"language", JsonValue::MakeString(config.language));

    JsonValue hotkey = JsonValue::MakeObject();
    hotkey.Set(L"hideToggle", HotkeySpecToJson(config.hotkey.hideToggle));
    hotkey.Set(L"openSettings", HotkeySpecToJson(config.hotkey.openSettings));
    root.Set(L"hotkey", std::move(hotkey));

    JsonValue runtime = JsonValue::MakeObject();
    runtime.Set(L"requireElevation", JsonValue::MakeBool(config.runtime.requireElevation));
    runtime.Set(L"startHidden", JsonValue::MakeBool(config.runtime.startHidden));
    runtime.Set(L"registerAutostart", JsonValue::MakeBool(config.runtime.registerAutostart));
    runtime.Set(L"pollIntervalMs", JsonValue::MakeNumber(config.runtime.pollIntervalMs));
    root.Set(L"runtime", std::move(runtime));

    // Global restore behaviour: bring every restored window to the foreground.
    // Lives at the same level as `runtime.*` so the rest of the schema (rules
    // with their own per-rule `restore` object) stays unaffected.
    JsonValue restore = JsonValue::MakeObject();
    restore.Set(L"bringToFront", JsonValue::MakeBool(config.bringRestoredToFront));
    root.Set(L"restore", std::move(restore));

    JsonValue rules = JsonValue::MakeArray();
    for (const auto& rule : config.rules) {
        rules.Push(RuleToJson(rule));
    }
    root.Set(L"rules", std::move(rules));
    return root;
}

bool JsonToConfig(const JsonValue& value, AppConfig& out, std::wstring& error) {
    error.clear();
    if (!value.IsObject()) {
        error = L"config root must be an object";
        return false;
    }
    AppConfig config = MakeDefaultConfig();
    if (const JsonValue* version = value.Find(L"version")) {
        if (version->IsNumber()) {
            config.version = static_cast<int>(version->AsInt());
        }
    }
    if (const JsonValue* language = value.Find(L"language")) {
        if (language->IsString() && !language->AsString().empty()) {
            config.language = language->AsString();
        }
    }
    if (const JsonValue* hotkey = value.Find(L"hotkey")) {
        if (const JsonValue* hideToggle = hotkey->Find(L"hideToggle")) {
            config.hotkey.hideToggle = ParseHotkeySpec(hideToggle);
        }
        if (const JsonValue* openSettings = hotkey->Find(L"openSettings")) {
            config.hotkey.openSettings = ParseHotkeySpec(openSettings);
        }
    }
    if (const JsonValue* runtime = value.Find(L"runtime")) {
        config.runtime.requireElevation = ReadBool(runtime->Find(L"requireElevation"), config.runtime.requireElevation);
        config.runtime.startHidden = ReadBool(runtime->Find(L"startHidden"), config.runtime.startHidden);
        config.runtime.registerAutostart = ReadBool(runtime->Find(L"registerAutostart"), config.runtime.registerAutostart);
        if (const JsonValue* poll = runtime->Find(L"pollIntervalMs")) {
            if (poll->IsNumber() && poll->AsInt() >= 100) {
                config.runtime.pollIntervalMs = static_cast<int>(poll->AsInt());
            }
        }
    }
    if (const JsonValue* restore = value.Find(L"restore")) {
        config.bringRestoredToFront =
            ReadBool(restore->Find(L"bringToFront"), config.bringRestoredToFront);
    }
    if (const JsonValue* rules = value.Find(L"rules")) {
        if (rules->IsArray()) {
            config.rules.clear();
            for (std::size_t i = 0; i < rules->Size(); ++i) {
                if ((*rules)[i].IsObject()) {
                    config.rules.push_back(RuleFromJson((*rules)[i]));
                }
            }
        }
    }
    // Self-heal: configs written by older builds can contain duplicate rule
    // ids, because OnAddRule() used to derive the id from rules_.size() + 1,
    // which rewinds after a rule is deleted. Repairing here (and letting the
    // next Save() persist the fixed ids) is the most reliable fix: rule.id is
    // only consumed as the display-name fallback in
    // SettingsWindow::RefreshRules and is referenced by neither state.json nor
    // any other module, so renaming an id has no side effects.
    NormalizeDuplicateRuleIds(config.rules);
    out = config;
    return true;
}

std::wstring GenerateUniqueRuleId(const std::vector<AppRule>& rules) {
    std::vector<std::wstring> taken;
    taken.reserve(rules.size());
    for (const AppRule& rule : rules) {
        taken.push_back(rule.id);
    }
    return FirstFreeRuleId(taken);
}

void NormalizeDuplicateRuleIds(std::vector<AppRule>& rules) {
    // Every id in the list is marked taken up front, so a freshly minted id can
    // neither collide with an already accepted one nor with a rule we have not
    // visited yet -- that keeps the first occurrence of each original id
    // untouched. The pass leaves all ids distinct, so a second pass accepts
    // everything as-is and changes nothing (idempotent).
    std::vector<std::wstring> taken;
    taken.reserve(rules.size());
    for (const AppRule& rule : rules) {
        taken.push_back(rule.id);
    }

    std::vector<std::wstring> seen;
    seen.reserve(rules.size());
    for (AppRule& rule : rules) {
        if (!Contains(seen, rule.id)) {
            seen.push_back(rule.id);
            continue;
        }
        const std::wstring fresh = FirstFreeRuleId(taken);
        rule.id = fresh;
        seen.push_back(fresh);
        taken.push_back(fresh);
    }
}

}  // namespace core
}  // namespace hideutil
