#include "core/WindowHider.h"

#include <windows.h>

namespace hideutil {
namespace core {

WindowHider::WindowHider() = default;

WindowHider::~WindowHider() {
    if (taskbar_ != nullptr) {
        taskbar_->Release();
        taskbar_ = nullptr;
    }
    taskbarReady_ = false;
}

bool WindowHider::Initialize() {
    if (taskbarReady_ && taskbar_ != nullptr) {
        return true;
    }
    ITaskbarList* taskbar = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&taskbar));
    if (FAILED(hr) || taskbar == nullptr) {
        return false;
    }
    hr = taskbar->HrInit();
    if (FAILED(hr)) {
        taskbar->Release();
        return false;
    }
    taskbar_ = taskbar;
    taskbarReady_ = true;
    return true;
}

bool WindowHider::IsWindowResponsive(HWND hwnd) const {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return false;
    }
    // A window on our own thread cannot hang us: the message would be
    // processed inline. (It also means the caller is the UI thread itself,
    // which by definition is pumping while dispatching this call.)
    if (GetWindowThreadProcessId(hwnd, nullptr) == GetCurrentThreadId()) {
        return true;
    }
    // Cross-thread probe: WM_NULL is a no-op message every pumping thread
    // answers immediately. SMTO_ABORTIFHUNG gives up right away on windows
    // already flagged "not responding"; otherwise we wait at most the probe
    // timeout for a suspended / hung target.
    DWORD_PTR sendResult = 0;
    const LRESULT sent = SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG,
                                             probeTimeoutMs_, &sendResult);
    return sent != 0;
}

UINT ResolveRestoreShowCmd(UINT showCmd, BOOL wasVisible, LONG_PTR prevStyle) {
    if (!wasVisible) {
        // A window that was already hidden has no show state to protect.
        return showCmd;
    }
    const bool invisibleShowCmd = (showCmd == SW_SHOWMINIMIZED) || (showCmd == SW_MINIMIZE) ||
                                  (showCmd == SW_HIDE) || (showCmd == SW_FORCEMINIMIZE);
    if (!invisibleShowCmd) {
        return showCmd;  // nothing to correct
    }
    // Was the window genuinely minimized at hide time? Then preserving that is
    // the correct behaviour -- the user had minimized it themselves.
    const bool genuinelyMinimized =
        ((prevStyle & WS_MINIMIZE) != 0) && ((prevStyle & WS_MAXIMIZE) == 0);
    if (genuinelyMinimized) {
        return showCmd;
    }
    // Transient / bogus minimized state: bring the window back to the visible
    // state its style bits describe.
    return ((prevStyle & WS_MAXIMIZE) != 0) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
}

WindowSnapshot WindowHider::SnapshotWindow(HWND hwnd) const {
    WindowSnapshot snapshot;
    snapshot.hwnd = hwnd;
    GetWindowThreadProcessId(hwnd, &snapshot.pid);
    snapshot.wasVisible = IsWindowVisible(hwnd) ? TRUE : FALSE;
    snapshot.prevStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
    snapshot.prevExStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    snapshot.placement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hwnd, &snapshot.placement);
    // Normalise at capture time so the persisted state never carries a
    // transient minimized placement (see ResolveRestoreShowCmd).
    snapshot.placement.showCmd = ResolveRestoreShowCmd(
        snapshot.placement.showCmd, snapshot.wasVisible, snapshot.prevStyle);
    snapshot.wasForeground = (GetForegroundWindow() == hwnd) ? TRUE : FALSE;
    snapshot.taskbarTabRemoved = FALSE;
    snapshot.altTabSuppressed = FALSE;
    return snapshot;
}

WindowOpResult WindowHider::ApplyHide(const WindowSnapshot& snapshot) {
    if (snapshot.hwnd == nullptr || !IsWindow(snapshot.hwnd)) {
        return WindowOpResult::InvalidWindow;
    }
    // Skip targets that are not pumping messages: the async show request
    // would sit unprocessed until the thread resumes anyway, and the caller
    // must know the hide was not applied (the entry stays in the snapshot).
    if (!IsWindowResponsive(snapshot.hwnd)) {
        return WindowOpResult::Unresponsive;
    }
    if (snapshot.taskbarTabRemoved) {
        RemoveTaskbarTab(snapshot.hwnd);
    }
    if (snapshot.altTabSuppressed) {
        SetToolWindow(snapshot.hwnd, true);
    }
    // Get the window off the screen by MINIMIZING it, not with SW_HIDE.
    //
    // SW_HIDE makes Office (Excel / Word) tear down its command-bar docks to
    // reclaim resources: the MsoCommandBar and MsoWorkPane shells are
    // destroyed, the ribbon's NUIPane is re-parented into a hidden 100x100
    // "parking" pane, and the ribbon container loses WS_VISIBLE. Showing the
    // window again does NOT rebuild any of it -- the ribbon stays gone until
    // the application is restarted, and no amount of re-showing, resizing or
    // Ctrl+F1 brings it back.
    //
    // Minimizing never triggers that teardown. Restoring a minimized Excel
    // always brings the ribbon back -- that is the everyday user experience,
    // and it is exactly the behaviour we need here. The window is still gone
    // from the screen, its taskbar button is removed separately
    // (RemoveTaskbarTab above) and it is kept out of Alt+Tab separately
    // (WS_EX_TOOLWINDOW above), so nothing leaks.
    //
    ShowWindowAsync(snapshot.hwnd, SW_MINIMIZE);
    return WindowOpResult::Ok;
}

WindowOpResult WindowHider::RestoreWindow(const WindowSnapshot& snapshot, bool bringToFront) {
    if (snapshot.hwnd == nullptr || !IsWindow(snapshot.hwnd)) {
        return WindowOpResult::InvalidWindow;
    }
    // A target that is not pumping messages (suspended / hung / lifecycle
    // suspended UWP) would either deadlock us or silently ignore the restore.
    // Skip it; the caller keeps the entry for a later retry.
    if (!IsWindowResponsive(snapshot.hwnd)) {
        return WindowOpResult::Unresponsive;
    }
    // Restore the original styles first, then force a frame recalculation so
    // that WS_EX_TOOLWINDOW removal takes effect immediately. The liveness
    // probe above guarantees the target is pumping, so a synchronous
    // SetWindowPos (no SWP_ASYNCWINDOWPOS) is safe AND necessary: it lets
    // the SWP_FRAMECHANGED apply atomically before the placement below,
    // which is the key to making the recovered window appear correctly
    // (avoiding the DWM black-frame / "needs-minimize-others" regression).
    SetWindowLongPtrW(snapshot.hwnd, GWL_STYLE, snapshot.prevStyle);
    SetWindowLongPtrW(snapshot.hwnd, GWL_EXSTYLE, snapshot.prevExStyle);
    SetWindowPos(snapshot.hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    if (snapshot.taskbarTabRemoved) {
        AddTaskbarTab(snapshot.hwnd);
    }

    // Was the window ALREADY minimized by the user before we hid it? That
    // state lives in the snapshot (ResolveRestoreShowCmd preserves it) and
    // must be left alone below.
    const bool userHadMinimized = (snapshot.placement.showCmd == SW_SHOWMINIMIZED);

    // Undo the minimization ApplyHide applied, BEFORE the geometry is pinned
    // down. Going through SW_RESTORE (instead of letting SetWindowPlacement do
    // everything) matters: it is the normal "put the window back" path, so the
    // application gets its WM_SHOWWINDOW / WM_SIZE sequence and rebuilds any
    // UI it parked while hidden -- Office in particular restores its ribbon
    // and status bar docks this way.
    if (snapshot.wasVisible && !userHadMinimized && IsIconic(snapshot.hwnd)) {
        ShowWindow(snapshot.hwnd, SW_RESTORE);
    }

    // SetWindowPlacement is the single source of truth for the recovered
    // geometry and show state. Its showCmd is the exact original state
    // captured at hide time (SW_SHOWNORMAL / SW_SHOWMAXIMIZED /
    // SW_SHOWMINIMIZED / SW_HIDE). Applying it once here means the window
    // appears at its real position and size, not at the default "centered
    // default size" that any subsequent ShowWindow(SW_SHOW/SW_RESTORE)
    // would otherwise force on top of it.
    WINDOWPLACEMENT placement = snapshot.placement;
    placement.length = sizeof(WINDOWPLACEMENT);
    // Defensive second pass: a snapshot written by an older build (or by a
    // hide that raced with the app's frame rebuild) can still carry a bogus
    // SW_SHOWMINIMIZED. Never let it minimize a window that was visible.
    placement.showCmd =
        ResolveRestoreShowCmd(placement.showCmd, snapshot.wasVisible, snapshot.prevStyle);
    SetWindowPlacement(snapshot.hwnd, &placement);

    if (!snapshot.wasVisible) {
        // Originally-hidden window: keep it hidden. The async hide is fine
        // here (the user does not see a transition).
        ShowWindowAsync(snapshot.hwnd, SW_HIDE);
    } else if (IsIconic(snapshot.hwnd) && !userHadMinimized) {
        // Belt and braces. If the window ended up minimized anyway (a placement
        // whose rcNormalPosition/ptMinPosition disagrees with its showCmd, or
        // an app that minimized itself while hidden), SW_RESTORE is the only
        // call that clears WS_MINIMIZE and puts it back at its real geometry --
        // SW_SHOW would leave the window minimized and off-screen.
        // A window the user had minimized themselves stays minimized.
        ShowWindow(snapshot.hwnd, SW_RESTORE);
    }
    // For the wasVisible==true path we deliberately do NOT issue
    // ShowWindow(SW_SHOW/SW_RESTORE): SetWindowPlacement above has already
    // applied the correct showCmd, and re-issuing a generic "show" would
    // clobber the carefully-restored geometry.

    // Force DWM to recomposite the recovered area. Without this, the
    // window can stay visually black / blank until the user wiggles
    // something else, because the async sequence (SetWindowLongPtrW +
    // SetWindowPos with FRAMECHANGED) leaves DWM one step behind.
    RedrawWindow(snapshot.hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);

    if (bringToFront) {
        // Best-effort. The system may refuse (e.g. the foreground lockout
        // window or another foreground process), and that is acceptable.
        SetForegroundWindow(snapshot.hwnd);
    }
    return WindowOpResult::Ok;
}

WindowOpResult WindowHider::HideWindow(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return WindowOpResult::InvalidWindow;
    }
    if (!IsWindowResponsive(hwnd)) {
        return WindowOpResult::Unresponsive;
    }
    ShowWindowAsync(hwnd, SW_HIDE);
    return WindowOpResult::Ok;
}

bool WindowHider::RemoveTaskbarTab(HWND hwnd) {
    if (!Initialize() || taskbar_ == nullptr) {
        return false;
    }
    return SUCCEEDED(taskbar_->DeleteTab(hwnd));
}

bool WindowHider::AddTaskbarTab(HWND hwnd) {
    if (!Initialize() || taskbar_ == nullptr) {
        return false;
    }
    return SUCCEEDED(taskbar_->AddTab(hwnd));
}

bool WindowHider::SetToolWindow(HWND hwnd, bool enable) {
    if (!IsWindow(hwnd)) {
        return false;
    }
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (enable) {
        exStyle = (exStyle | WS_EX_TOOLWINDOW) & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
    } else {
        exStyle = exStyle & ~static_cast<LONG_PTR>(WS_EX_TOOLWINDOW);
    }
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    // SWP_ASYNCWINDOWPOS: never block on a cross-thread target.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);
    return true;
}

}  // namespace core
}  // namespace hideutil
