// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

#include "core/WindowHider.h"

#include <windows.h>

#include <dwmapi.h>

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

void SanitizePlacementFlags(WINDOWPLACEMENT& placement) {
    // WPF_RESTORETOMAXIMIZED only makes sense while the showCmd is minimized.
    // With a visible showCmd it makes SetWindowPlacement (and Office) take the
    // minimize path and re-create the WS_MINIMIZE|WS_MAXIMIZE double-bit
    // window parked at (-32000,-32000).
    if (placement.showCmd != SW_SHOWMINIMIZED) {
        placement.flags &= ~static_cast<UINT>(WPF_RESTORETOMAXIMIZED);
    }
}

namespace {

// Best-effort DWM transition switch. Failures are ignored on purpose: when
// DWM is unavailable there is no animation to suppress in the first place.
void SetTransitionsDisabled(HWND hwnd, BOOL disabled) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }
    const BOOL value = disabled;
    DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &value, sizeof(value));
}

}  // namespace

void SuppressWindowTransitions(HWND hwnd) { SetTransitionsDisabled(hwnd, TRUE); }

void RestoreWindowTransitions(HWND hwnd) { SetTransitionsDisabled(hwnd, FALSE); }

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
    // The normalisation above may have turned a transient minimized showCmd
    // into a visible one; the matching flag must not survive that change.
    SanitizePlacementFlags(snapshot.placement);
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
    // Bounded verification: the minimize takes effect only when the target
    // thread pumps. If the user toggles back to restore before that happens,
    // RestoreWindow would restore the window and this stale SW_MINIMIZE
    // would then land AFTER the restore -- orphaning a minimized window
    // whose snapshot was already erased. Close that race by verifying the
    // window actually reached its iconic state before returning (bounded,
    // best-effort: on timeout the entry is still kept, and restore's
    // belt-and-braces SW_RESTORE remains the safety net). Same-thread
    // callers (the unit tests) pump on their own and are skipped.
    DWORD targetThread = 0;
    GetWindowThreadProcessId(snapshot.hwnd, &targetThread);
    if (targetThread != 0 && targetThread != GetCurrentThreadId()) {
        for (int i = 0; i < 60; ++i) {  // ~1.5s worst case
            if (!IsWindow(snapshot.hwnd) || IsIconic(snapshot.hwnd)) {
                break;
            }
            Sleep(25);
        }
    }
    // Apply the Alt+Tab suppression only AFTER the window is off-screen.
    // Setting WS_EX_TOOLWINDOW (with SWP_FRAMECHANGED) on a still-visible
    // Office window makes Office rebuild its entire main frame -- fresh
    // HWNDs, born minimized -- which then has to be chased and restored.
    // Field window-tree captures make this concrete: with the suppression
    // off the XLMAIN handles stayed stable across hide/restore; with it on
    // they were recreated. Doing the style change once the window is
    // minimized avoids that churn.
    if (snapshot.altTabSuppressed) {
        SetToolWindow(snapshot.hwnd, true);
    }
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
    // Suppress the system's minimize / restore / maximize animation for this
    // window before any visibility change, so the recovery plays no visible
    // transition -- in particular the maximize re-assert further down would
    // otherwise animate on every single restore. Reset by RestoreAll once the
    // whole batch has settled (the attribute is per window and must not leak).
    SuppressWindowTransitions(snapshot.hwnd);
    // Capture the window's REAL on-screen state BEFORE prevStyle is written
    // back below: a stale snapshot's prevStyle itself carries WS_MINIMIZE,
    // and applying it would make IsIconic start reporting TRUE even though
    // the window is currently on screen -- which would defeat the
    // stale-entry self-heal further down.
    const bool iconicBeforeStyleRestore = IsIconic(snapshot.hwnd);
    const LONG_PTR styleBeforeRestore = GetWindowLongPtrW(snapshot.hwnd, GWL_STYLE);
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

    // A window we hid must come back VISIBLE -- never minimized.
    //
    // Earlier revisions tried to preserve a "the user had minimized it"
    // state, but that state is indistinguishable from our own leaked
    // minimize: ApplyHide minimizes, and Office can rebuild its whole frame
    // (fresh HWNDs) while minimized, so the rebuilt frame is captured
    // minimized by the watchdog and then "preserved" on restore -- stranding
    // the target as a tiny fragment at the bottom of the screen with no
    // taskbar button. Two P0 field failures came from that policy, so it is
    // retired: any minimized/hidden showCmd on a window that was visible at
    // hide time is converted to the visible state its captured style /
    // placement flags describe.
    const bool claimsInvisible =
        (snapshot.placement.showCmd == SW_SHOWMINIMIZED) ||
        (snapshot.placement.showCmd == SW_MINIMIZE) ||
        (snapshot.placement.showCmd == SW_HIDE) ||
        (snapshot.placement.showCmd == SW_FORCEMINIMIZE);

    // SetWindowPlacement is the single source of truth for the recovered
    // geometry and show state. Applying it once means the window appears at
    // its real position and size, not at the default "centered default size"
    // that a generic ShowWindow(SW_SHOW) would force on top of it.
    WINDOWPLACEMENT placement = snapshot.placement;
    placement.length = sizeof(WINDOWPLACEMENT);
    placement.showCmd =
        ResolveRestoreShowCmd(placement.showCmd, snapshot.wasVisible, snapshot.prevStyle);
    if (snapshot.wasVisible && claimsInvisible) {
        const bool restoreMaximized =
            ((snapshot.prevStyle & WS_MAXIMIZE) != 0) ||
            ((snapshot.placement.flags & WPF_RESTORETOMAXIMIZED) != 0) ||
            ((styleBeforeRestore & WS_MAXIMIZE) != 0);
        placement.showCmd = restoreMaximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    }
    // A legacy persisted snapshot (written before this sanitation existed) can
    // still carry WPF_RESTORETOMAXIMIZED alongside a visible showCmd.
    SanitizePlacementFlags(placement);

    if (snapshot.wasVisible) {
        if (bringToFront) {
            // Activating restore: the caller explicitly wants the window to
            // take the foreground, so use the normal "put it back" path --
            // SW_RESTORE hands the app its WM_SHOWWINDOW / WM_SIZE sequence and
            // lets Office rebuild the UI it parked while hidden.
            if (iconicBeforeStyleRestore) {
                ShowWindow(snapshot.hwnd, SW_RESTORE);
            }
            SetWindowPlacement(snapshot.hwnd, &placement);
            if (IsIconic(snapshot.hwnd)) {
                // Belt and braces: a hidden target must never be left minimized.
                ShowWindow(snapshot.hwnd, SW_RESTORE);
            }
            // SetWindowPlacement(SW_SHOWMAXIMIZED) is not honoured reliably for
            // Office: the window comes back as a normal (restored) window parked
            // at rcNormalPosition, losing the maximized state it had before
            // being hidden. Re-assert the maximized state explicitly.
            if (placement.showCmd == SW_SHOWMAXIMIZED && !IsZoomed(snapshot.hwnd)) {
                ShowWindow(snapshot.hwnd, SW_MAXIMIZE);
            }
        } else {
            // Non-activating restore (the default): the user's current window
            // keeps the keyboard focus, so no visible takeover happens.
            //
            // Primitive choice is measurement-driven (the "DIAG" experiment in
            // tests/core_tests.cpp): ShowWindow(SW_SHOWNOACTIVATE) and
            // SetWindowPos(SWP_SHOWWINDOW) do NOT leave the iconic state, and
            // ShowWindow(SW_MAXIMIZE) on a minimized window does not either --
            // only SetWindowPlacement with the no-activate showCmd restores the
            // window to its captured geometry WITHOUT taking the foreground
            // (SetWindowPlacement activates only for the activating showCmds;
            // SW_RESTORE / SW_SHOWMAXIMIZED would).
            WINDOWPLACEMENT noActivate = placement;
            noActivate.length = sizeof(WINDOWPLACEMENT);
            noActivate.showCmd = SW_SHOWNOACTIVATE;
            SetWindowPlacement(snapshot.hwnd, &noActivate);
            if (placement.showCmd == SW_SHOWMAXIMIZED && !IsZoomed(snapshot.hwnd)) {
                // WS_MAXIMIZE can only be set through a ShowWindow command, and
                // SW_SHOWMAXIMIZED is documented as activating -- so this last
                // step may still take the foreground. It runs with the geometry
                // already correct, and RestoreAll's z-order hand-back returns
                // the foreground to the user's window either way.
                ShowWindow(snapshot.hwnd, SW_MAXIMIZE);
            }
            if (IsIconic(snapshot.hwnd)) {
                // The no-activate placement did not leave the minimized state:
                // correctness first -- fall back to the activating restore
                // rather than stranding the window minimized.
                ShowWindow(snapshot.hwnd, SW_RESTORE);
                SetWindowPlacement(snapshot.hwnd, &placement);
            }
        }
    } else {
        // Originally-hidden window: it must simply stay hidden. Its captured
        // placement is deliberately NOT applied -- a minimized/hidden showCmd
        // here is exactly what produced the desynced WS_MINIMIZE|WS_MAXIMIZE
        // "visible but minimized" window (visible, yet unclickable and
        // unrecoverable). The geometry of a never-shown window does not
        // matter, and re-showing it would tear down Office command bars.
        ShowWindowAsync(snapshot.hwnd, SW_HIDE);
    }
    // For the wasVisible==true path we deliberately do NOT issue a generic
    // ShowWindow(SW_SHOW): SetWindowPlacement above has already applied the
    // correct showCmd, and re-issuing a generic "show" would clobber the
    // carefully-restored geometry.

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
