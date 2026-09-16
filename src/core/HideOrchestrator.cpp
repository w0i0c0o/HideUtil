#include "core/HideOrchestrator.h"

#include <windows.h>

#include <algorithm>
#include <chrono>

namespace hideutil {
namespace core {

namespace {

void AddPidUnique(std::vector<DWORD>& values, DWORD value) {
    if (value != 0 && std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

bool ContainsPid(const std::vector<DWORD>& values, DWORD value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool HasSnapshotContent(const GlobalSnapshot& snapshot) {
    return !snapshot.windows.empty() || !snapshot.audio.empty() ||
           !snapshot.processes.empty() || !snapshot.jumpListPids.empty();
}

const WindowSnapshot* FindWindowSnapshot(const std::vector<WindowSnapshot>& list, HWND hwnd) {
    for (const WindowSnapshot& item : list) {
        if (item.hwnd == hwnd) {
            return &item;
        }
    }
    return nullptr;
}

const AudioSnapshot* FindAudioSnapshot(const std::vector<AudioSnapshot>& list,
                                       const std::wstring& sessionId) {
    for (const AudioSnapshot& item : list) {
        if (item.sessionId == sessionId) {
            return &item;
        }
    }
    return nullptr;
}

const ProcessSnapshot* FindProcessSnapshot(const std::vector<ProcessSnapshot>& list, DWORD pid) {
    for (const ProcessSnapshot& item : list) {
        if (item.pid == pid) {
            return &item;
        }
    }
    return nullptr;
}

// Drop base entries whose window has been destroyed: such a window can never be
// restored, so it is safe to discard. This decision is made ONLY on IsWindow and
// never on "absent from this round's targets" (an unobserved window may belong
// to another hide batch and must be kept).
GlobalSnapshot DropDestroyedWindows(const GlobalSnapshot& base) {
    GlobalSnapshot kept;
    kept.hiddenAt = base.hiddenAt;
    kept.moveToVirtualDesktopApplied = base.moveToVirtualDesktopApplied;
    kept.audio = base.audio;
    kept.processes = base.processes;
    kept.jumpListPids = base.jumpListPids;
    for (const WindowSnapshot& window : base.windows) {
        if (window.hwnd != nullptr && IsWindow(window.hwnd)) {
            kept.windows.push_back(window);
        }
    }
    return kept;
}

}  // namespace

HideOrchestrator::HideOrchestrator(AppRuleStore& ruleStore, StateStore& stateStore,
                                   ProcessWindowResolver& resolver)
    : ruleStore_(ruleStore), stateStore_(stateStore), resolver_(resolver) {}

HideOrchestrator::~HideOrchestrator() {
    processWatcher_.Stop();
}

const wchar_t* HideOrchestrator::StateName() const {
    switch (state_) {
        case State::Visible: return L"VISIBLE";
        case State::Hiding: return L"HIDING";
        case State::Hidden: return L"HIDDEN";
        case State::Restoring: return L"RESTORING";
    }
    return L"UNKNOWN";
}

std::size_t HideOrchestrator::HiddenWindowCount() const {
    return stateStore_.Snapshot().windows.size();
}

std::wstring HideOrchestrator::EffectiveStateFile() const {
    return stateFile_.empty() ? StateStore::DefaultStatePath() : stateFile_;
}

HideOrchestrator::PersistedStatus HideOrchestrator::QueryPersistedState() const {
    PersistedStatus status;
    StateStore probe;
    if (probe.RestoreFromFile(EffectiveStateFile()) && !probe.Snapshot().empty()) {
        status.hasSnapshot = true;
        status.windowCount = probe.Snapshot().windows.size();
        status.audioCount = probe.Snapshot().audio.size();
    }
    return status;
}

GlobalSnapshot HideOrchestrator::LoadBaseSnapshot() const {
    // A snapshot persisted on disk is authoritative across processes.
    if (!EffectiveStateFile().empty()) {
        StateStore probe;
        if (probe.RestoreFromFile(EffectiveStateFile()) && HasSnapshotContent(probe.Snapshot())) {
            return probe.Snapshot();
        }
    }
    // Fall back to the in-memory committed snapshot (same-process repeat).
    return stateStore_.Snapshot();
}

bool HideOrchestrator::HideAll() {
    if (state_ == State::Hidden || state_ == State::Hiding) {
        return true;  // idempotent (same process)
    }

    // An existing snapshot is authoritative: a repeated hide (often from a fresh
    // process, where the in-memory state machine restarts at Visible) must merge
    // INTO it, never replace it with a smaller snapshot. Otherwise the second
    // hide would re-snapshot already-hidden windows with wasVisible=false and
    // destroy the state needed to restore them.
    const TargetSet targets = resolver_.ResolveTargets(ruleStore_);
    if (targets.empty()) {
        // Nothing matched: report a no-op failure and neither enter HIDDEN nor
        // touch the persisted snapshot (leaves any existing state recoverable).
        return false;
    }

    // Normalise the base before merging. Both steps only ever remove entries
    // that can no longer be restored; neither removes entries merely because
    // they are absent from this round's targets (those may belong to another
    // hide batch and must survive for restore):
    //   1. Drop entries whose window was destroyed (IsWindow == FALSE).
    //   2. Drop entries whose HWND was recycled by a different process (same
    //      HWND, different PID) so the recycled window is re-snapshotted as new
    //      instead of inheriting a stale wasVisible.
    GlobalSnapshot base = DropDestroyedWindows(LoadBaseSnapshot());
    base = StateStore::PruneStaleWindows(base, targets.windows);
    const bool haveBase = HasSnapshotContent(base);

    state_ = State::Hiding;

    windowHider_.Initialize();
    audioMuter_.Initialize();
    desktopMover_.Initialize();

    std::vector<WindowSnapshot> observedWindows;  // new windows to append
    std::vector<WindowSnapshot> windowsToHide;    // windows to (re)hide now

    for (const WindowRef& ref : targets.windows) {
        if (ref.hwnd == nullptr || !IsWindow(ref.hwnd)) {
            continue;
        }
        // A rule may request only a desktop move or only a process suspend; do
        // not hide a window unless the rule actually asked to hide it.
        const bool doHide = (ref.rule == nullptr) || ref.rule->actions.hideWindow;
        const bool doMove = (ref.rule != nullptr) && ref.rule->actions.moveToVirtualDesktop;
        if (!doHide && !doMove) {
            continue;
        }

        // Already snapshotted: the persisted entry is authoritative. Keep it
        // verbatim (especially wasVisible / prevStyle / placement / taskbar and
        // alt-tab flags) and never re-snapshot it. If the app itself re-showed
        // the window, hide it again using the stored entry.
        const WindowSnapshot* stored = FindWindowSnapshot(base.windows, ref.hwnd);
        if (stored != nullptr) {
            if (doHide && IsWindowVisible(ref.hwnd)) {
                windowsToHide.push_back(*stored);
            }
            continue;
        }

        // New window: capture it and record it. Only hide a window that was
        // actually visible; an already-invisible window is recorded with
        // wasVisible=false (it was not hidden by us) but left alone.
        WindowSnapshot snapshot = windowHider_.SnapshotWindow(ref.hwnd);
        if (ref.rule != nullptr) {
            snapshot.taskbarTabRemoved = doHide ? TRUE : FALSE;
            snapshot.altTabSuppressed = (doHide && ref.rule->actions.hideFromAltTab) ? TRUE : FALSE;
        }
        if (doMove) {
            std::wstring prevDesktopId;
            if (desktopMover_.MoveToOtherDesktop(ref.hwnd, prevDesktopId)) {
                snapshot.movedToDesktop = TRUE;
                snapshot.prevDesktopId = std::move(prevDesktopId);
            }
        }
        observedWindows.push_back(snapshot);
        if (doHide && snapshot.wasVisible) {
            windowsToHide.push_back(snapshot);
        }
    }

    // Audio: mute again (idempotent) but only snapshot sessions not yet recorded.
    std::vector<AudioSnapshot> observedAudio;
    for (DWORD pid : targets.mutePids) {
        const std::vector<AudioSnapshot> sessions = audioMuter_.SnapshotSessions(pid);
        for (const AudioSnapshot& session : sessions) {
            if (FindAudioSnapshot(base.audio, session.sessionId) != nullptr) {
                continue;
            }
            observedAudio.push_back(session);
        }
        audioMuter_.MuteSessions(pid);
    }
    audioMuter_.RegisterSessionNotification(targets.mutePids);

    // Publish the union so a crash mid-hide still leaves a recoverable
    // snapshot. Windows are recorded BEFORE they are hidden (a spurious
    // "restore" of a never-hidden window is a harmless no-op; the reverse --
    // hidden without a record -- would lose the window forever). Processes
    // are deliberately NOT suspended yet: see the suspend loop further down.
    GlobalSnapshot merged = StateStore::MergeSnapshots(base, observedWindows, observedAudio,
                                                       {}, targets.jumpListPids);
    if (!haveBase) {
        merged.hiddenAt = std::chrono::system_clock::now();
    }
    stateStore_.SetSnapshot(merged);
    if (!stateFile_.empty()) {
        stateStore_.Persist(stateFile_);
    }

    for (DWORD pid : targets.trayPids) {
        trayHider_.ApplyHide(pid);
    }

    // Apply the hides BEFORE any suspension. The window operations are
    // non-blocking (ShowWindowAsync / SWP_ASYNCWINDOWPOS), but a suspended
    // target thread will not process the posted messages until it is resumed,
    // so every window must be hidden first.
    for (const WindowSnapshot& window : windowsToHide) {
        windowHider_.ApplyHide(window);
    }

    winEventHook_.Install(targets.pids, [this](HWND hwnd, DWORD pid) {
        OnWindowShown(hwnd, pid);
    });

    // Suspend LAST, after every window operation completes: touching a window
    // of a suspended process can never take effect. Only suspend processes we
    // have not already recorded -- re-suspending would bump the kernel
    // suspend count and break the paired single resume.
    for (DWORD pid : targets.suspendPids) {
        if (FindProcessSnapshot(base.processes, pid) != nullptr) {
            continue;
        }
        if (FindProcessSnapshot(stateStore_.MutableSnapshot().processes, pid) != nullptr) {
            continue;  // already recorded during this pass
        }
        if (processController_.Suspend(pid)) {
            ProcessSnapshot process;
            process.pid = pid;
            process.wasSuspended = TRUE;
            stateStore_.MutableSnapshot().processes.push_back(process);
        }
    }
    // Re-persist the final state including the suspensions.
    if (!stateFile_.empty()) {
        stateStore_.Persist(stateFile_);
    }

    targetPids_ = targets.pids;
    for (const WindowSnapshot& window : merged.windows) {
        AddPidUnique(targetPids_, window.pid);
    }

    processWatcher_.Start(ruleStore_, ruleStore_.Config().runtime.pollIntervalMs,
                          [this](DWORD pid, const std::wstring& name) {
                              if (targetStartedHandler_) {
                                  targetStartedHandler_(pid, name);
                              } else {
                                  OnTargetProcessStarted(pid, name);
                              }
                          });

    state_ = State::Hidden;
    return true;
}

void HideOrchestrator::PartitionWindowsForRestore(const std::vector<WindowSnapshot>& windows,
                                                  const std::function<WindowProbe(HWND)>& probe,
                                                  std::vector<WindowSnapshot>& restorable,
                                                  std::vector<WindowSnapshot>& deferred) {
    for (const WindowSnapshot& window : windows) {
        switch (probe(window.hwnd)) {
            case WindowProbe::Responsive:
                restorable.push_back(window);
                break;
            case WindowProbe::Unresponsive:
                deferred.push_back(window);
                break;
            case WindowProbe::Destroyed:
                // The window is gone and can never be restored: drop it.
                break;
        }
    }
}

bool HideOrchestrator::RestoreAll() {
    if (state_ == State::Restoring) {
        return true;
    }
    if (state_ == State::Visible) {
        // Possible cross-process restore: reload a previously persisted snapshot.
        if (stateFile_.empty()) {
            return true;  // idempotent no-op
        }
        if (!stateStore_.RestoreFromFile(EffectiveStateFile()) || stateStore_.Snapshot().empty()) {
            return true;
        }
    }
    state_ = State::Restoring;

    processWatcher_.Stop();
    winEventHook_.Uninstall();
    audioMuter_.UnregisterSessionNotification();

    const GlobalSnapshot snapshot = stateStore_.Load();

    // Remember the window the user is currently working in (e.g. WPS). A
    // hidden window can never be the foreground window, so this is always
    // the user's own window -- never one of the windows we are about to
    // bring back.
    const HWND originalForeground = GetForegroundWindow();

    // Resume suspended processes FIRST (before windows are re-shown): a
    // suspended owner never pumps messages, so its windows could otherwise
    // never be restored. A resume that failed while the process still exists
    // stays in the snapshot so the next restore retries it.
    std::vector<ProcessSnapshot> remainingProcesses;
    for (const ProcessSnapshot& process : snapshot.processes) {
        if (!ProcessController::ShouldResume(process)) {
            continue;
        }
        if (processController_.Resume(process.pid)) {
            continue;
        }
        HANDLE probe = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.pid);
        if (probe != nullptr) {
            CloseHandle(probe);
            remainingProcesses.push_back(process);
        }
        // Process no longer exists: nothing left to resume, drop the entry.
    }

    audioMuter_.RestoreSessions(snapshot.audio);

    std::vector<DWORD> trayPids;
    for (const WindowSnapshot& window : snapshot.windows) {
        AddPidUnique(trayPids, window.pid);
    }
    for (DWORD pid : trayPids) {
        trayHider_.Restore(pid);
    }

    // Partition BEFORE touching any window: no cross-thread window operation
    // (not even the virtual-desktop move) may be issued against a target
    // whose thread is not pumping messages -- it would block the UI thread
    // forever. Unresponsive windows are deferred, never lost.
    std::vector<WindowSnapshot> restorable;
    std::vector<WindowSnapshot> deferred;
    PartitionWindowsForRestore(
        snapshot.windows,
        [this](HWND hwnd) -> WindowProbe {
            if (hwnd == nullptr || !IsWindow(hwnd)) {
                return WindowProbe::Destroyed;
            }
            return windowHider_.IsWindowResponsive(hwnd) ? WindowProbe::Responsive
                                                         : WindowProbe::Unresponsive;
        },
        restorable, deferred);

    // Move windows back to their original virtual desktop.
    for (const WindowSnapshot& window : restorable) {
        if (window.movedToDesktop && !window.prevDesktopId.empty()) {
            desktopMover_.RestoreToDesktop(window.hwnd, window.prevDesktopId);
        }
    }

    for (const WindowSnapshot& window : restorable) {
        const WindowOpResult result =
            windowHider_.RestoreWindow(window, ruleStore_.Config().bringRestoredToFront);
        if (result == WindowOpResult::Unresponsive) {
            // Lost the race: the target stopped pumping between the partition
            // and the restore. Keep the entry for the next attempt.
            deferred.push_back(window);
        }
        // Ok -> restored. InvalidWindow -> destroyed, can never come back.
    }

    // Give the foreground back to the user when they did not ask for
    // bringToFront. SetWindowPlacement (WindowHider::RestoreWindow) has no
    // "no-activate" option -- unlike SetWindowPos(SWP_NOACTIVATE) or
    // ShowWindow(SW_SHOWNOACTIVATE) -- so restoring a window inevitably
    // activates it. Doing the hand-back ONCE here, after every window is
    // restored, both fixes the "restored window stole my foreground" bug
    // and avoids flickering the foreground between restored windows.
    //
    // Two complementary repairs, applied only when bringToFront is off:
    //   1. SetForegroundWindow returns input focus to the user's window. The
    //      system may deny this under the foreground lockout -- that is fine.
    //   2. SetWindowPos(originalForeground, HWND_TOP, ..., SWP_NOACTIVATE)
    //      explicitly restores the z-order. Crucially this does NOT require
    //      the foreground right, so it works even when (1) is denied: the
    //      user's window is pushed to the top of the z-order WITHOUT being
    //      activated, which guarantees every restored window -- including a
    //      multi-top-level-window target such as WPS, whose windows were
    //      activated in restore order and left fragmented above the user's
    //      window -- ends up BELOW it, and their relative z-order (set by
    //      the restore sequence) is preserved. This is the fix for the
    //      "window decomposition" z-order fragmentation seen in the field.
    //
    // Note on topmost windows: a target's WS_EX_TOPMOST bit is already
    // round-tripped through WindowSnapshot::prevExStyle (captured at hide
    // time and re-applied by WindowHider::RestoreWindow), so topmost state
    // is preserved across hide/restore without any extra bookkeeping. The
    // z-order repair above only reorders non-topmost windows, which matches
    // the pre-hide stacking (a genuinely topmost target stays above the
    // user's window, exactly as it did before it was hidden).
    if (!ruleStore_.Config().bringRestoredToFront && originalForeground != nullptr &&
        IsWindow(originalForeground)) {
        if (GetForegroundWindow() != originalForeground) {
            // Best-effort: hand the keyboard focus back. If the shell denies
            // it (foreground lockout), the z-order repair below still runs.
            SetForegroundWindow(originalForeground);
        }
        // z-order repair, independent of the foreground lockout.
        SetWindowPos(originalForeground, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    // One-way privacy cleanup (default off): clear the target's jump list.
    for (DWORD pid : snapshot.jumpListPids) {
        jumpListCleaner_.ClearForProcess(pid);
    }

    // Only a fully successful restore may erase the snapshot. Deferred windows
    // (and processes that could not be resumed) are re-persisted so the next
    // restore -- or the startup auto-restore -- retries them. The persisted
    // snapshot stays authoritative (docs/02 section 8.1).
    GlobalSnapshot remainder;
    remainder.hiddenAt = snapshot.hiddenAt;
    remainder.windows = std::move(deferred);
    remainder.processes = std::move(remainingProcesses);
    if (!remainder.windows.empty() || !remainder.processes.empty()) {
        stateStore_.SetSnapshot(remainder);
        if (!stateFile_.empty()) {
            stateStore_.Persist(stateFile_);
        }
    } else {
        stateStore_.Clear();
        if (!stateFile_.empty()) {
            DeleteFileW(EffectiveStateFile().c_str());
        }
    }
    targetPids_.clear();

    state_ = State::Visible;
    return true;
}

void HideOrchestrator::OnTargetProcessStarted(DWORD pid, const std::wstring& /*processName*/) {
    if (state_ != State::Hidden || pid == 0) {
        return;
    }
    const AppRule* rule = ruleStore_.MatchByPid(pid);
    if (rule == nullptr) {
        return;
    }
    const std::vector<HWND> windows = resolver_.CollectWindowsForPid(pid);
    for (HWND hwnd : windows) {
        if (hwnd == nullptr || !IsWindow(hwnd)) {
            continue;
        }
        WindowSnapshot snapshot = windowHider_.SnapshotWindow(hwnd);
        snapshot.taskbarTabRemoved = rule->actions.hideWindow ? TRUE : FALSE;
        snapshot.altTabSuppressed = rule->actions.hideFromAltTab ? TRUE : FALSE;
        if (rule->actions.moveToVirtualDesktop) {
            std::wstring prevDesktopId;
            if (desktopMover_.MoveToOtherDesktop(hwnd, prevDesktopId)) {
                snapshot.movedToDesktop = TRUE;
                snapshot.prevDesktopId = std::move(prevDesktopId);
            }
        }
        stateStore_.MutableSnapshot().windows.push_back(snapshot);
        windowHider_.ApplyHide(snapshot);
    }
    if (rule->actions.muteAudio) {
        audioMuter_.MuteSessions(pid);
    }
    if (rule->actions.hideTrayIcon) {
        trayHider_.ApplyHide(pid);
    }
    if (rule->actions.suspendProcess && processController_.Suspend(pid)) {
        ProcessSnapshot process;
        process.pid = pid;
        process.wasSuspended = TRUE;
        stateStore_.MutableSnapshot().processes.push_back(process);
    }
    if (rule->actions.clearJumpList) {
        stateStore_.MutableSnapshot().jumpListPids.push_back(pid);
    }
    AddPidUnique(targetPids_, pid);
}

void HideOrchestrator::OnWindowShown(HWND hwnd, DWORD pid) {
    if (state_ != State::Hidden) {
        return;
    }
    if (!ContainsPid(targetPids_, pid)) {
        return;
    }
    // The target app re-showed its own window: hide it again.
    windowHider_.HideWindow(hwnd);
}

}  // namespace core
}  // namespace hideutil
