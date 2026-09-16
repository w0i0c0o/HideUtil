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

}  // namespace core
}  // namespace hideutil
