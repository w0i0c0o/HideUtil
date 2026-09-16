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
