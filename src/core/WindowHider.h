// Window hiding handler: snapshot / hide / restore for top-level windows.
//
// CROSS-THREAD SAFETY (P0): SetWindowPos without SWP_ASYNCWINDOWPOS and
// ShowWindow synchronously send messages (WM_WINDOWPOSCHANGING / WM_SHOWWINDOW)
// to the thread that owns the target window and block until that thread
// processes them. If the target thread is not pumping messages -- a suspended
// process, a hung app, or a UWP app lifecycle-suspended by the OS once all of
// its windows are invisible -- the calling (UI) thread deadlocks forever.
// Therefore every operation on a TARGET window must either be made async
// (SWP_ASYNCWINDOWPOS / ShowWindowAsync) or be preceded by a responsiveness
// probe (SendMessageTimeoutW + SMTO_ABORTIFHUNG).
#pragma once

#include "core/Types.h"

#include <shobjidl.h>

namespace hideutil {
namespace core {

// Outcome of a window operation, so callers can distinguish "done" from
// "skipped because the target is not pumping messages" (kept for retry).
enum class WindowOpResult {
    Ok,            // operation issued successfully
    InvalidWindow, // hwnd is null or already destroyed (can never be restored)
    Unresponsive,  // target thread not pumping messages; skipped, keep entry
};

// Choose the showCmd that must be applied when a window is brought back.
//
// Office applications (Excel) transiently report SW_SHOWMINIMIZED from
// GetWindowPlacement while they rebuild their top-level frame -- which is
// exactly what SetToolWindow triggers with SWP_FRAMECHANGED just before the
// window is hidden. If that transient state is honoured, SetWindowPlacement
// restores the window MINIMIZED and parked at (-32000,-32000): the user sees
// the workbook collapse to a 160x28 sliver and the ribbon "disappear", and
// there is no ShowWindow afterwards to undo it.
//
// Rule: a window that was visible when we hid it must come back visible. Only
// a GENUINELY minimized window (WS_MINIMIZE set, WS_MAXIMIZE clear) is allowed
// to be recorded / restored as minimized.
//
// Pure function so it can be unit-tested without real windows.
UINT ResolveRestoreShowCmd(UINT showCmd, BOOL wasVisible, LONG_PTR prevStyle);

// Clear placement flags that contradict the showCmd we are about to apply.
//
// WPF_RESTORETOMAXIMIZED means "currently minimized; the next restore goes to
// maximized". Office (Excel) reports it on the transient minimized placement
// it emits while rebuilding its frame -- and the capture-time normalisation
// above converts that showCmd to a VISIBLE one (SW_SHOWMAXIMIZED). If the flag
// is left in place the placement becomes self-contradictory (visible showCmd +
// "restore to maximized"), and SetWindowPlacement -- and Office itself --
// resolve it down the MINIMIZE path, recreating the desynced
// WS_MINIMIZE|WS_MAXIMIZE window parked at (-32000,-32000). That is why a
// maximized Office window failed to come back while a normal one was fine.
//
// Pure function so it can be unit-tested without real windows.
void SanitizePlacementFlags(WINDOWPLACEMENT& placement);

class WindowHider {
public:
    WindowHider();
    ~WindowHider();

    WindowHider(const WindowHider&) = delete;
    WindowHider& operator=(const WindowHider&) = delete;

    // Create the ITaskbarList COM object (must be called on an STA thread).
    bool Initialize();

    // Capture the original state of a window.
    WindowSnapshot SnapshotWindow(HWND hwnd) const;

    // Probe whether the target window's thread is pumping messages. Returns
    // false for null / destroyed windows and for windows whose owning thread
    // does not answer WM_NULL within the probe timeout. Never blocks longer
    // than the probe timeout.
    bool IsWindowResponsive(HWND hwnd) const;

    // Probe timeout for IsWindowResponsive, in milliseconds (default 1000).
    void SetProbeTimeoutMs(DWORD timeoutMs) { probeTimeoutMs_ = timeoutMs; }
    DWORD ProbeTimeoutMs() const { return probeTimeoutMs_; }

    // Hide the window per the snapshot flags (taskbar tab / tool window /
    // hide). Non-blocking: skips unresponsive targets instead of hanging.
    WindowOpResult ApplyHide(const WindowSnapshot& snapshot);

    // Restore GWL_STYLE / GWL_EXSTYLE / placement / visibility / taskbar tab.
    // `bringToFront` requests SetForegroundWindow after the window is back.
    // Skips unresponsive targets (caller keeps the entry for retry). Never
    // blocks: the liveness probe gates all cross-thread window operations.
    WindowOpResult RestoreWindow(const WindowSnapshot& snapshot, bool bringToFront);

    // Re-hide helper used by the WinEvent hook when an app re-shows itself.
    WindowOpResult HideWindow(HWND hwnd);

    bool RemoveTaskbarTab(HWND hwnd);
    bool AddTaskbarTab(HWND hwnd);
    bool SetToolWindow(HWND hwnd, bool enable);

private:
    ITaskbarList* taskbar_ = nullptr;
    bool taskbarReady_ = false;
    DWORD probeTimeoutMs_ = 1000;
};

}  // namespace core
}  // namespace hideutil
