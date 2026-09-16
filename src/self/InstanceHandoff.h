// Second-instance hand-off for HideUtil.exe.
//
// Single-instance protection stays exactly as it was: the first process owns
// Local\HideUtil_SingleInstance_Mutex and every later launch exits. What this
// module fixes is *how* that second launch behaves:
//
//   G1  the command line used to be discarded and the exit code was always 0;
//   G2  a minimised settings window was never restored (IsIconic -> SW_RESTORE);
//   G3  PostMessage(HWND_BROADCAST) cannot reach a message-only window, so the
//       broadcast channel was dead weight (the named event is the real signal);
//   G4  a second launch produced no feedback at all;
//   G5  when the running instance was hung but still held the mutex, the second
//       launch silently exited -- indistinguishable from success.
//
// The hand-off is therefore: write the intent to a request file, signal the
// running instance through the existing named event, and wait for an
// acknowledgement event that the running instance sets only *after* it has
// actually handled the request. A timeout turns the silent failure into an
// explicit prompt.
//
// Everything that can be unit tested is a pure function over strings (argument
// classification, JSON round trip); only the Win32 waiting / signalling touches
// the OS.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace hideutil {
namespace self {

// Exit codes for HideUtil.exe. `InternalError` and the CLI's own codes (2 =
// zero matches) are unchanged; 0/3/4/5 are the new hand-off outcomes.
enum class LaunchExitCode : int {
    HandedOff = 0,      // intent forwarded and acknowledged by the running instance
    InternalError = 1,  // unchanged
    NoAcknowledge = 3,  // forwarded, but the running instance never answered
    ArgRejected = 4,    // a state-changing switch was refused, nothing forwarded
    CannotConnect = 5,  // the running instance could not be reached at all
};

// ---------------------------------------------------------------------------
// C. Command-line classification (pure, no OS interaction).
// ---------------------------------------------------------------------------

enum class LaunchIntent {
    Settings,  // open the settings window (default, and explicit --settings)
    Rejected,  // a state-changing switch that must never be forwarded
};

struct ParsedLaunchArgs {
    LaunchIntent intent = LaunchIntent::Settings;
    // Set when intent == Rejected: the exact token that caused the refusal.
    std::wstring rejectedSwitch;
    // Forwarded page selection: "" (default) / "hotkeys" / "rules".
    std::wstring page;
    // True only when --settings was passed explicitly. The GUI must NOT open
    // the settings window on a bare launch, but a second instance with no
    // arguments must still forward "open settings" (the historical behaviour).
    bool explicitSettings = false;
};

// Splits `commandLine` into whitespace / quote-aware tokens. Pure and exported
// so the tokeniser itself is unit testable.
std::vector<std::wstring> TokenizeCommandLine(const std::wstring& commandLine);

// True for the two page names that may be forwarded.
bool IsForwardablePage(const std::wstring& page);

// True for switches that are refused instead of forwarded: they change hidden
// state, and letting a stray shortcut reveal hidden windows would be a
// back door into a privacy tool. Those belong to hideutil_cli.exe.
bool IsRejectedSwitch(const std::wstring& token);

// Classify a command line. Rejection wins over everything else, because a
// refused switch must never be acted on just because a friendly switch was
// also present. Unknown tokens are ignored (stay permissive).
ParsedLaunchArgs ParseLaunchArgs(const std::wstring& commandLine);

// ---------------------------------------------------------------------------
// C. Wake request file: %APPDATA%\HideUtil\wake-request.json
// ---------------------------------------------------------------------------

struct WakeRequest {
    DWORD pid = 0;
    std::wstring action = L"settings";  // "settings" is the only action today
    std::wstring page;                  // "" / "hotkeys" / "rules"
    // Monotonic (GetTickCount64) write time in milliseconds. Used to reject a
    // request that has been lying around: acting on one would pop the settings
    // window without the user asking, which is unacceptable for a hiding tool.
    ULONGLONG writtenAtMs = 0;
};

// A request older than this is discarded instead of executed. Deliberately
// short: a real hand-off is consumed within milliseconds.
constexpr ULONGLONG kMaxWakeRequestAgeMs = 10000;

// Outcome of consuming a wake request. `Stale` must NOT be treated like
// `Missing`: falling back to "open the settings window" would recreate the
// unprompted popup this expiry exists to prevent.
enum class WakeRequestStatus {
    Consumed,  // a fresh request was read; act on it
    Stale,     // a request existed but was too old: discarded, do nothing
    Missing,   // no usable request: fall back to the default behaviour
};

std::wstring WakeRequestPath();
std::wstring LastInstancePath();

// Pure serialisation / deserialisation (no file system access).
std::wstring SerializeWakeRequest(const WakeRequest& request);
bool DeserializeWakeRequest(const std::wstring& text, WakeRequest& out);

// Pure: is a request written at `writtenAtMs` too old to act on at `nowMs`?
// Both values come from the monotonic GetTickCount64 clock, so the unsigned
// subtraction also behaves sensibly across the 49-day wrap.
bool IsWakeRequestStale(ULONGLONG writtenAtMs, ULONGLONG nowMs);

// Writes the request atomically: a temporary file is written first and then
// moved over the target, so a reader can never observe a half-written file.
// The write time is stamped here so it can never be left unset.
bool WriteWakeRequest(const std::wstring& path, const WakeRequest& request);

// Reads the request and deletes it immediately -- the request is one-shot, and
// a leftover file must never replay an old intent on a later wake. `out` is
// left untouched unless the status is `Consumed`.
WakeRequestStatus ReadAndConsumeWakeRequest(const std::wstring& path, WakeRequest& out);

// Deletes any leftover request file (called once at startup). Returns true when
// there is nothing left behind afterwards.
bool DiscardPendingWakeRequest();

// ---------------------------------------------------------------------------
// E. Stale-copy marker: %APPDATA%\HideUtil\last-instance.json
// ---------------------------------------------------------------------------

struct LastInstanceInfo {
    DWORD pid = 0;
    std::wstring path;        // GetModuleFileNameW of the running instance
    long long startedAt = 0;  // epoch seconds
};

std::wstring SerializeLastInstance(const LastInstanceInfo& info);
bool DeserializeLastInstance(const std::wstring& text, LastInstanceInfo& out);
bool WriteLastInstance(const std::wstring& path, const LastInstanceInfo& info);
bool ReadLastInstance(const std::wstring& path, LastInstanceInfo& out);

// Records the calling process in last-instance.json. Best effort: a failure
// only costs the "which copy is running" hint in the unresponsive prompt.
bool WriteOwnInstanceMarker();

// ---------------------------------------------------------------------------
// B. Handshake (Win32).
// ---------------------------------------------------------------------------

// Auto-reset acknowledgement event. Both sides create it with CreateEventW so
// it does not matter which side starts first -- whoever wins the race owns the
// object and the other gets a second handle to the same one.
const wchar_t* WakeAckEventName();

// How long a second instance waits for the acknowledgement before it decides
// the running instance is unresponsive.
constexpr DWORD kAckTimeoutMs = 1500;

// Pure: builds the text of the "instance is not responding" prompt. Pass a
// non-empty `runningPath` to have the running copy's location appended (used
// when it differs from this executable -- the "different build is running"
// case). Kept pure so the wording is unit testable without a message box.
std::wstring BuildUnresponsivePrompt(const std::wstring& runningPath);

// Pure: builds the text shown when a refused switch is passed. A refusal that
// produces no visible feedback is indistinguishable from "the launch did
// nothing", which is exactly the silent-failure class this round removes.
std::wstring BuildRejectedSwitchMessage(const std::wstring& switchName);

// Shows the refusal dialog and returns the ArgRejected exit code. Used by both
// the first-instance and the second-instance paths so the feedback is identical.
int ReportRejectedSwitch(const std::wstring& switchName);

struct HandoffResult {
    LaunchExitCode exitCode = LaunchExitCode::HandedOff;
    bool acknowledged = false;
    // Filled from last-instance.json when it could be read; used to name the
    // running copy in the unresponsive prompt (and to end it on request).
    LastInstanceInfo runningInstance;
    bool runningInstanceKnown = false;
    // True when the user chose to end the unresponsive instance.
    bool endedRunningInstance = false;
};

// Full second-instance flow: classify -> (refuse?) -> record -> signal ->
// wait for the ack -> prompt on timeout. Never throws.
HandoffResult RunSecondInstanceHandoff(const std::wstring& commandLine);

// Ends `pid` after verifying that it really is the recorded HideUtil image.
// A recycled PID must never take down an unrelated process, so the image path
// has to match before anything is terminated.
bool TerminateInstanceByPid(DWORD pid, const std::wstring& expectedImagePath);

// Best-effort relaunch of this executable with the given command line.
bool RelaunchOwnInstance(const std::wstring& arguments);

}  // namespace self
}  // namespace hideutil
