// Loads / saves %APPDATA%\HideUtil\config.json and performs rule matching.
#pragma once

#include "core/Json.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace hideutil {
namespace core {

class AppRuleStore {
public:
    AppRuleStore();

    // Default per-user config location: %APPDATA%\HideUtil\config.json.
    static std::wstring DefaultConfigPath();

    // Load configuration from a path. Returns false (and leaves the previous
    // configuration intact) if the file is missing or malformed.
    bool Load(const std::wstring& path);
    bool LoadDefault();

    // Persist the current configuration as UTF-8 JSON.
    bool Save(const std::wstring& path) const;

    // Create the default config file when it does not exist yet.
    bool EnsureDefaultConfig(const std::wstring& path);

    const AppConfig& Config() const { return config_; }
    AppConfig& MutableConfig() { return config_; }

    const std::vector<AppRule>& Rules() const { return config_.rules; }
    void SetRules(std::vector<AppRule> rules) { config_.rules = std::move(rules); }
    void AddRule(const AppRule& rule) { config_.rules.push_back(rule); }

    // Returns the first enabled rule that matches the given process/window
    // attributes, honouring matchMode (any / all). Null when nothing matches.
    const AppRule* Match(const std::wstring& exePath,
                         const std::wstring& processName,
                         const std::wstring& windowClass) const;

    // Convenience: resolves the exe path / image name for a PID then matches.
    const AppRule* MatchByPid(DWORD pid) const;

    const AppRule* MatchByExePath(const std::wstring& exePath) const;

    // Rule matching helper (exposed for unit tests).
    static bool RuleMatches(const AppRule& rule,
                            const std::wstring& exePath,
                            const std::wstring& processName,
                            const std::wstring& windowClass);

private:
    AppConfig config_;
};

// Build the default configuration (Ctrl+Alt+F12 toggle / Ctrl+Alt+F11 settings).
AppConfig MakeDefaultConfig();

// Serialize / parse a single config document.
JsonValue ConfigToJson(const AppConfig& config);
bool JsonToConfig(const JsonValue& value, AppConfig& out, std::wstring& error);

// Returns "rule-N" where N is the smallest positive integer not already
// taken by an existing rule. Never returns a colliding id.
std::wstring GenerateUniqueRuleId(const std::vector<AppRule>& rules);

// In-place repair of duplicate ids loaded from an older or damaged config:
// keeps the first occurrence of each id untouched and re-assigns later
// duplicates. Idempotent -- running it twice must not change anything.
void NormalizeDuplicateRuleIds(std::vector<AppRule>& rules);

}  // namespace core
}  // namespace hideutil
