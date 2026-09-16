// In-memory (optionally persisted) store for the original-state snapshot.
#pragma once

#include "core/Types.h"

#include <string>

namespace hideutil {
namespace core {

class StateStore {
public:
    StateStore();

    // Build a fresh snapshot; subsequent Add* calls accumulate into it.
    void BeginSnapshot();
    void AddWindow(const WindowSnapshot& snapshot);
    void AddAudio(const AudioSnapshot& snapshot);
    void AddProcess(const ProcessSnapshot& snapshot);
    void AddJumpListPid(DWORD pid);

    // Finalise the pending snapshot and make it the committed snapshot.
    void Commit();

    // Read access to the committed snapshot.
    const GlobalSnapshot& Snapshot() const { return committed_; }
    GlobalSnapshot& MutableSnapshot() { return committed_; }
    GlobalSnapshot Load() const { return committed_; }

    // Publish `snapshot` as the committed snapshot wholesale. Used to store a
    // merged union produced by MergeSnapshots so that a snapshot already on
    // disk is never replaced by a smaller one.
    void SetSnapshot(const GlobalSnapshot& snapshot);

    // Pure union merge (no Win32 calls): every entry already present in `base`
    // is authoritative and preserved verbatim; observations whose key is not in
    // `base` are appended. Keys: window = HWND, audio = sessionId,
    // process / jump list = pid. The result is always a superset of `base`.
    static GlobalSnapshot MergeSnapshots(const GlobalSnapshot& base,
                                         const std::vector<WindowSnapshot>& observedWindows,
                                         const std::vector<AudioSnapshot>& observedAudio,
                                         const std::vector<ProcessSnapshot>& observedProcesses,
                                         const std::vector<DWORD>& observedJumpListPids);

    // Pure, Win32-free stale-window pruning. Removes only entries that can no
    // longer be restored, never entries that are merely absent this round:
    //   - base entry whose HWND matches an observed window with a DIFFERENT pid
    //     -> HWND was recycled; drop it (caller re-snapshots it as a new window).
    //   - base entry whose HWND + pid both match an observation -> keep verbatim.
    //   - base entry whose HWND is not observed this round -> keep (it may belong
    //     to another hide batch and must survive for restore).
    // Note: Windows that have been destroyed (IsWindow == FALSE) are filtered by
    // the Win32 caller before this runs; this function must never decide on any
    // basis other than the HWND/pid keys.
    static GlobalSnapshot PruneStaleWindows(const GlobalSnapshot& base,
                                            const std::vector<WindowRef>& observed);

    void Clear();
    bool IsEmpty() const { return committed_.empty(); }
    bool IsBegun() const { return begun_; }

    // Optional persistence for crash recovery / cross-process restore.
    bool Persist(const std::wstring& path) const;
    bool RestoreFromFile(const std::wstring& path);
    static std::wstring DefaultStatePath();

private:
    GlobalSnapshot pending_;
    GlobalSnapshot committed_;
    bool begun_ = false;
};

}  // namespace core
}  // namespace hideutil
