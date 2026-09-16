// SetWinEventHook(EVENT_OBJECT_SHOW) wrapper used to re-hide windows that the
// target application re-shows on its own while hidden.
#pragma once

#include <windows.h>

#include <functional>
#include <vector>

namespace hideutil {
namespace core {

class WinEventHook {
public:
    using ShownCallback = std::function<void(HWND hwnd, DWORD pid)>;

    WinEventHook();
    ~WinEventHook();

    WinEventHook(const WinEventHook&) = delete;
    WinEventHook& operator=(const WinEventHook&) = delete;

    // Install the out-of-context show hook filtered to the given PIDs.
    // An empty filter installs a hook that ignores every event (no-op).
    bool Install(const std::vector<DWORD>& pidFilter, ShownCallback callback);
    void Uninstall();
    bool IsInstalled() const { return hook_ != nullptr; }

private:
    static void CALLBACK WinEventProcThunk(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                           LONG idObject, LONG idChild, DWORD eventThread,
                                           DWORD eventTime);
    void OnWinEvent(HWND hwnd, LONG idObject);

    HWINEVENTHOOK hook_ = nullptr;
    std::vector<DWORD> pids_;
    ShownCallback callback_;

    static WinEventHook* instance_;
};

// Re-hide policy for the EVENT_OBJECT_SHOW watchdog.
//
// EVENT_OBJECT_SHOW is raised for EVERY HWND that gets shown, child windows
// included -- it is not limited to top-level windows. Composite UIs (Office
// ribbon, browser chrome, IDE docks) rebuild and re-show their child panes
// whenever the frame is recalculated, which is exactly what
// WindowHider::SetToolWindow triggers with SWP_FRAMECHANGED right before the
// window is hidden.
//
// Hiding such a child pane is invisible while its parent is hidden, but the
// pane stays hidden after the parent comes back, and because only top-level
// windows are ever enumerated into the snapshot, a hidden child has no
// restore path at all -- the pane is lost until the application is restarted.
//
// Therefore: only a window that IS the root of its own window tree may be
// re-hidden. Callers pass GetAncestor(hwnd, GA_ROOT) so this stays a pure
// comparison and remains unit-testable.
//
// GA_ROOT walks the PARENT chain only (not the owner chain), so a window
// owned by the target's main window -- a dialog or flyout -- still counts as
// top-level and keeps being re-hidden, preserving the leak protection.
bool ShouldRehideWindow(HWND hwnd, HWND rootAncestor);

}  // namespace core
}  // namespace hideutil
