#include "core/WinEventHook.h"

#include <algorithm>

namespace hideutil {
namespace core {

WinEventHook* WinEventHook::instance_ = nullptr;

WinEventHook::WinEventHook() = default;

WinEventHook::~WinEventHook() {
    Uninstall();
}

bool WinEventHook::Install(const std::vector<DWORD>& pidFilter, ShownCallback callback) {
    Uninstall();
    pids_ = pidFilter;
    callback_ = std::move(callback);

    // WINEVENT_OUTOFCONTEXT delivers events to the calling thread's message
    // queue, which keeps the callback serialised with the UI thread.
    hook_ = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, &WinEventProcThunk,
                            0, 0, WINEVENT_OUTOFCONTEXT);
    if (hook_ == nullptr) {
        callback_ = nullptr;
        pids_.clear();
        return false;
    }
    instance_ = this;
    return true;
}

void WinEventHook::Uninstall() {
    if (hook_ != nullptr) {
        UnhookWinEvent(hook_);
        hook_ = nullptr;
    }
    if (instance_ == this) {
        instance_ = nullptr;
    }
    pids_.clear();
    callback_ = nullptr;
}

void CALLBACK WinEventHook::WinEventProcThunk(HWINEVENTHOOK /*hook*/, DWORD /*event*/, HWND hwnd,
                                              LONG idObject, LONG /*idChild*/, DWORD /*eventThread*/,
                                              DWORD /*eventTime*/) {
    if (instance_ == nullptr) {
        return;
    }
    instance_->OnWinEvent(hwnd, idObject);
}

void WinEventHook::OnWinEvent(HWND hwnd, LONG idObject) {
    // Only care about whole-window show events.
    if (idObject != OBJID_WINDOW || hwnd == nullptr) {
        return;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return;
    }
    if (std::find(pids_.begin(), pids_.end(), pid) == pids_.end()) {
        return;
    }
    // Never re-hide a child window. See ShouldRehideWindow for the full
    // rationale: hiding a child pane of a composite UI (Excel's ribbon is the
    // motivating case) silently destroys it for good, because children are
    // never part of the snapshot and therefore never restored.
    if (!ShouldRehideWindow(hwnd, GetAncestor(hwnd, GA_ROOT))) {
        return;
    }
    if (callback_) {
        callback_(hwnd, pid);
    }
}

bool ShouldRehideWindow(HWND hwnd, HWND rootAncestor) {
    if (hwnd == nullptr || rootAncestor == nullptr) {
        // nullptr hwnd, and a destroyed window (GetAncestor returns nullptr).
        return false;
    }
    return hwnd == rootAncestor;
}

}  // namespace core
}  // namespace hideutil
