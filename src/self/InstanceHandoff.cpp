#include "self/InstanceHandoff.h"

#include "core/Json.h"
#include "self/Logger.h"
#include "self/SelfShell.h"

#include "i18n/Localizer.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include <ctime>

namespace hideutil {
namespace self {

namespace {

using L10n = hideutil::i18n::Localizer;

// Auto-reset: one wake, one acknowledgement.
const wchar_t* kAckEventName = L"Local\\HideUtil_WakeAck_Event";

const wchar_t* kWakeRequestFile = L"\\HideUtil\\wake-request.json";
const wchar_t* kLastInstanceFile = L"\\HideUtil\\last-instance.json";

// %APPDATA%\HideUtil\<file>, falling back to a relative name when neither the
// known folder nor the environment variable is available (same degradation as
// AppRuleStore::DefaultConfigPath / StateStore::DefaultStatePath).
std::wstring AppDataFilePath(const wchar_t* fileName) {
    PWSTR appData = nullptr;
    std::wstring base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)) &&
        appData != nullptr) {
        base.assign(appData);
        CoTaskMemFree(appData);
    } else {
        wchar_t buffer[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH) > 0) {
            base.assign(buffer);
        }
    }
    if (base.empty()) {
        // Skip the leading backslash so the fallback stays a plain file name.
        return std::wstring(fileName + 1);
    }
    return base + fileName;
}

// Creates the parent directory of `path` if it is missing. A failure is not
// fatal: the write below reports the real problem.
void EnsureParentDirectory(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
    }
}

long long NowEpochSeconds() {
    return static_cast<long long>(std::time(nullptr));
}

// Writes `text` to `path` atomically: a sibling temporary file is written
// first and then moved over the target, so a concurrent reader can never see a
// partially written document.
bool WriteFileAtomic(const std::wstring& path, const std::wstring& text) {
    EnsureParentDirectory(path);
    const std::wstring temp = path + L".tmp";
    if (!core::WriteTextFileUtf8(temp, text)) {
        return false;
    }
    if (MoveFileExW(temp.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

std::wstring OwnModulePath() {
    wchar_t buffer[MAX_PATH * 2] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0) {
        return std::wstring();
    }
    return std::wstring(buffer, length);
}

// Reads a JSON object member that may be absent or of the wrong type.
const core::JsonValue* OptionalMember(const core::JsonValue& root, const std::wstring& key) {
    const core::JsonValue* member = root.Find(key);
    if (member == nullptr) {
        return nullptr;
    }
    return member;
}

}  // namespace

// ---------------------------------------------------------------------------
// C. Command-line classification
// ---------------------------------------------------------------------------

std::vector<std::wstring> TokenizeCommandLine(const std::wstring& commandLine) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    bool inQuotes = false;
    for (const wchar_t ch : commandLine) {
        if (ch == L'"') {
            inQuotes = !inQuotes;
            continue;
        }
        if (!inQuotes && (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r')) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

bool IsForwardablePage(const std::wstring& page) {
    return page == L"hotkeys" || page == L"rules";
}

bool IsRejectedSwitch(const std::wstring& token) {
    // Exact token matching on purpose: --settings-dump must be refused and must
    // NOT be mistaken for --settings by a substring test (the old
    // CommandLineHasSwitch helper did exactly that).
    return token == L"--hide" || token == L"--restore" || token == L"--status" ||
           token == L"--suspend" || token == L"--resume" || token == L"--settings-dump";
}

ParsedLaunchArgs ParseLaunchArgs(const std::wstring& commandLine) {
    ParsedLaunchArgs parsed;
    for (const std::wstring& token : TokenizeCommandLine(commandLine)) {
        if (token == L"--settings") {
            parsed.explicitSettings = true;
            continue;
        }
        if (token.rfind(L"--page=", 0) == 0) {
            const std::wstring value = token.substr(7);
            // An unknown or empty page silently degrades to the default view
            // instead of refusing the whole launch.
            if (IsForwardablePage(value)) {
                parsed.page = value;
            }
            continue;
        }
        if (IsRejectedSwitch(token)) {
            parsed.intent = LaunchIntent::Rejected;
            parsed.rejectedSwitch = token;
            return parsed;
        }
        // Anything else is ignored: unknown switches must not break launching.
    }
    // No arguments at all behaves like --settings for the purposes of a
    // hand-off (the historical behaviour of SignalExistingInstance).
    return parsed;
}

// ---------------------------------------------------------------------------
// C. Wake request
// ---------------------------------------------------------------------------

std::wstring WakeRequestPath() { return AppDataFilePath(kWakeRequestFile); }

std::wstring LastInstancePath() { return AppDataFilePath(kLastInstanceFile); }

std::wstring SerializeWakeRequest(const WakeRequest& request) {
    core::JsonValue root = core::JsonValue::MakeObject();
    root.Set(L"pid", core::JsonValue::MakeNumber(static_cast<double>(request.pid)));
    root.Set(L"action", core::JsonValue::MakeString(request.action));
    root.Set(L"page", core::JsonValue::MakeString(request.page));
    root.Set(L"writtenAtMs", core::JsonValue::MakeNumber(static_cast<double>(request.writtenAtMs)));
    return root.Dump();
}

bool DeserializeWakeRequest(const std::wstring& text, WakeRequest& out) {
    core::JsonValue root;
    std::wstring error;
    if (!core::JsonValue::Parse(text, root, error) || !root.IsObject()) {
        return false;
    }
    WakeRequest parsed;
    const core::JsonValue* pid = OptionalMember(root, L"pid");
    if (pid != nullptr && pid->IsNumber()) {
        parsed.pid = static_cast<DWORD>(pid->AsInt());
    }
    const core::JsonValue* action = OptionalMember(root, L"action");
    if (action != nullptr && action->IsString()) {
        parsed.action = action->AsString();
    }
    const core::JsonValue* page = OptionalMember(root, L"page");
    if (page != nullptr && page->IsString()) {
        parsed.page = page->AsString();
    }
    const core::JsonValue* writtenAt = OptionalMember(root, L"writtenAtMs");
    if (writtenAt != nullptr && writtenAt->IsNumber()) {
        parsed.writtenAtMs = static_cast<ULONGLONG>(writtenAt->AsInt());
    }
    if (parsed.action.empty()) {
        parsed.action = L"settings";
    }
    if (!IsForwardablePage(parsed.page)) {
        parsed.page.clear();  // unknown page -> default view
    }
    out = parsed;
    return true;
}

bool IsWakeRequestStale(ULONGLONG writtenAtMs, ULONGLONG nowMs) {
    // Unsigned subtraction is intentional: it also yields a huge (therefore
    // stale) age if `writtenAtMs` is somehow in the future, and it stays correct
    // across the monotonic counter's 49-day wrap.
    return (nowMs - writtenAtMs) > kMaxWakeRequestAgeMs;
}

bool WriteWakeRequest(const std::wstring& path, const WakeRequest& request) {
    if (path.empty()) {
        return false;
    }
    // Stamp here so the timestamp can never be left unset by a caller.
    WakeRequest stamped = request;
    stamped.writtenAtMs = GetTickCount64();
    return WriteFileAtomic(path, SerializeWakeRequest(stamped));
}

WakeRequestStatus ReadAndConsumeWakeRequest(const std::wstring& path, WakeRequest& out) {
    std::wstring text;
    if (!core::ReadTextFileUtf8(path, text)) {
        // Nothing usable there; make sure a corrupt file cannot linger.
        DeleteFileW(path.c_str());
        return WakeRequestStatus::Missing;
    }
    // One-shot: consume it before interpreting it, whatever we decide to do.
    DeleteFileW(path.c_str());

    WakeRequest parsed;
    if (!DeserializeWakeRequest(text, parsed)) {
        return WakeRequestStatus::Missing;
    }
    if (IsWakeRequestStale(parsed.writtenAtMs, GetTickCount64())) {
        // A leftover from a wake that was never acknowledged. Acting on it now
        // would open the settings window without the user asking -- the exact
        // unprompted exposure a hiding tool must avoid.
        Logger::Warn(L"discarding a stale wake request");
        return WakeRequestStatus::Stale;
    }
    out = parsed;
    return WakeRequestStatus::Consumed;
}

bool DiscardPendingWakeRequest() {
    const std::wstring path = WakeRequestPath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return true;  // nothing left behind
    }
    if (DeleteFileW(path.c_str()) == FALSE) {
        Logger::Warn(L"could not remove a leftover wake-request.json");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// E. Stale-copy marker
// ---------------------------------------------------------------------------

std::wstring SerializeLastInstance(const LastInstanceInfo& info) {
    core::JsonValue root = core::JsonValue::MakeObject();
    root.Set(L"pid", core::JsonValue::MakeNumber(static_cast<double>(info.pid)));
    root.Set(L"path", core::JsonValue::MakeString(info.path));
    root.Set(L"startedAt", core::JsonValue::MakeNumber(static_cast<double>(info.startedAt)));
    return root.Dump();
}

bool DeserializeLastInstance(const std::wstring& text, LastInstanceInfo& out) {
    core::JsonValue root;
    std::wstring error;
    if (!core::JsonValue::Parse(text, root, error) || !root.IsObject()) {
        return false;
    }
    LastInstanceInfo parsed;
    const core::JsonValue* pid = OptionalMember(root, L"pid");
    if (pid != nullptr && pid->IsNumber()) {
        parsed.pid = static_cast<DWORD>(pid->AsInt());
    }
    const core::JsonValue* path = OptionalMember(root, L"path");
    if (path != nullptr && path->IsString()) {
        parsed.path = path->AsString();
    }
    const core::JsonValue* startedAt = OptionalMember(root, L"startedAt");
    if (startedAt != nullptr && startedAt->IsNumber()) {
        parsed.startedAt = startedAt->AsInt();
    }
    out = parsed;
    return true;
}

bool WriteLastInstance(const std::wstring& path, const LastInstanceInfo& info) {
    if (path.empty()) {
        return false;
    }
    return WriteFileAtomic(path, SerializeLastInstance(info));
}

bool ReadLastInstance(const std::wstring& path, LastInstanceInfo& out) {
    std::wstring text;
    if (!core::ReadTextFileUtf8(path, text)) {
        return false;
    }
    return DeserializeLastInstance(text, out);
}

bool WriteOwnInstanceMarker() {
    LastInstanceInfo info;
    info.pid = GetCurrentProcessId();
    info.path = OwnModulePath();
    info.startedAt = NowEpochSeconds();
    const bool ok = WriteLastInstance(LastInstancePath(), info);
    if (!ok) {
        Logger::Warn(L"failed to write last-instance.json (stale-copy detection disabled)");
    }
    return ok;
}

// ---------------------------------------------------------------------------
// B. Handshake
// ---------------------------------------------------------------------------

const wchar_t* WakeAckEventName() { return kAckEventName; }

bool TerminateInstanceByPid(DWORD pid, const std::wstring& expectedImagePath) {
    if (pid == 0 || pid == GetCurrentProcessId()) {
        return false;  // never end ourselves
    }
    if (expectedImagePath.empty()) {
        // Without a recorded image we cannot tell a recycled PID from the real
        // instance, so refuse instead of risking an unrelated process.
        return false;
    }
    HANDLE process =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
    if (process == nullptr) {
        return false;
    }
    bool terminated = false;
    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD size = static_cast<DWORD>(std::size(buffer));
    if (QueryFullProcessImageNameW(process, 0, buffer, &size) != FALSE) {
        if (_wcsicmp(buffer, expectedImagePath.c_str()) == 0) {
            terminated = (TerminateProcess(process, 1) != FALSE);
        } else {
            Logger::Warn(L"refusing to end PID (image does not match the recorded instance)");
        }
    } else {
        Logger::Warn(L"refusing to end PID (image path could not be verified)");
    }
    CloseHandle(process);
    return terminated;
}

std::wstring BuildUnresponsivePrompt(const std::wstring& runningPath) {
    std::wstring message = L10n::Get("instance.unresponsive");
    if (!runningPath.empty()) {
        message += L"\n\n" + L10n::Get("instance.unresponsivePath") + L"\n" + runningPath;
    }
    return message;
}

std::wstring BuildRejectedSwitchMessage(const std::wstring& switchName) {
    std::wstring message = L10n::Format(L10n::Get("instance.rejectedSwitch"), switchName);
    message += L"\n    hideutil_cli.exe";
    return message;
}

int ReportRejectedSwitch(const std::wstring& switchName) {
    const std::wstring message = BuildRejectedSwitchMessage(switchName);
    MessageBoxW(nullptr, message.c_str(), L"HideUtil",
                MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
    return static_cast<int>(LaunchExitCode::ArgRejected);
}

bool RelaunchOwnInstance(const std::wstring& arguments) {
    const std::wstring modulePath = OwnModulePath();
    if (modulePath.empty()) {
        return false;
    }
    // ShellExecuteW (not CreateProcessW) so the manifest's requireAdministrator
    // level is honoured by the shell exactly as it is for a manual launch.
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", modulePath.c_str(),
                                           arguments.c_str(), nullptr, SW_SHOWNORMAL);
    // ShellExecuteW returns a value > 32 on success.
    return reinterpret_cast<INT_PTR>(result) > 32;
}

HandoffResult RunSecondInstanceHandoff(const std::wstring& commandLine) {
    HandoffResult result;

    const ParsedLaunchArgs args = ParseLaunchArgs(commandLine);
    if (args.intent == LaunchIntent::Rejected) {
        // G1/D: refuse, and ALWAYS tell the user. Exiting silently here would be
        // indistinguishable from "the launch did nothing" -- the exact
        // silent-failure class this hand-off exists to remove. No request file
        // is written and nothing is forwarded.
        Logger::Warn(L"refusing to forward a state-changing switch: " + args.rejectedSwitch);
        result.exitCode = LaunchExitCode::ArgRejected;
        ReportRejectedSwitch(args.rejectedSwitch);
        return result;
    }

    // Learn where the running copy lives before trying to wake it: if the wake
    // turns out to be unanswered, this is what lets us name (and end) it.
    LastInstanceInfo known;
    if (ReadLastInstance(LastInstancePath(), known)) {
        result.runningInstance = known;
        result.runningInstanceKnown = true;
    }

    HANDLE wakeEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, SelfShell::ShowSettingsEventName());
    if (wakeEvent == nullptr) {
        // D/G5: we know another instance exists (we lost the mutex) but there is
        // no way to talk to it. Report it instead of pretending success.
        Logger::Error(L"another instance holds the mutex but the wake event is unreachable");
        result.exitCode = LaunchExitCode::CannotConnect;
        return result;
    }

    // Created, not opened: whichever side starts first owns the object and the
    // other side gets a second handle to the same event.
    HANDLE ackEvent = CreateEventW(nullptr, FALSE, FALSE, kAckEventName);

    WakeRequest request;
    request.pid = GetCurrentProcessId();
    request.action = L"settings";
    request.page = args.page;
    if (!WriteWakeRequest(WakeRequestPath(), request)) {
        Logger::Warn(L"failed to write wake-request.json; falling back to a plain wake");
    }

    SetEvent(wakeEvent);

    if (ackEvent != nullptr) {
        const DWORD wait = WaitForSingleObject(ackEvent, kAckTimeoutMs);
        if (wait == WAIT_OBJECT_0) {
            result.acknowledged = true;
            result.exitCode = LaunchExitCode::HandedOff;
        }
    }

    if (!result.acknowledged) {
        // G4/G5: the running instance never answered. Surface it.
        const std::wstring own = OwnModulePath();
        const std::wstring& running = result.runningInstance.path;
        // Only mention the path when it differs from this copy: that is the
        // "you launched a different build" case worth pointing out (E).
        std::wstring pathToShow;
        if (result.runningInstanceKnown && !running.empty() &&
            (own.empty() || _wcsicmp(running.c_str(), own.c_str()) != 0)) {
            pathToShow = running;
        }
        const std::wstring message = BuildUnresponsivePrompt(pathToShow);
        const int answer = MessageBoxW(nullptr, message.c_str(), L"HideUtil",
                                       MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND);
        if (answer == IDYES) {
            if (result.runningInstanceKnown &&
                TerminateInstanceByPid(result.runningInstance.pid, result.runningInstance.path)) {
                result.endedRunningInstance = true;
                RelaunchOwnInstance(commandLine);
                result.exitCode = LaunchExitCode::HandedOff;
            } else {
                // Could not end it (unknown PID, or the image did not match).
                result.exitCode = LaunchExitCode::NoAcknowledge;
            }
        } else {
            result.exitCode = LaunchExitCode::NoAcknowledge;
        }
    }

    if (ackEvent != nullptr) {
        CloseHandle(ackEvent);
    }
    CloseHandle(wakeEvent);
    return result;
}

}  // namespace self
}  // namespace hideutil
