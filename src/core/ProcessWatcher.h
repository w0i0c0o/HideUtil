// Polling process watcher: re-applies rules when a hidden target restarts.
//
// The design allows either WMI (Win32_ProcessStartTrace) or polling. Polling is
// chosen for the default path: simpler, no extra COM, reliable. Runs on its own
// thread with a stoppable wait so Stop() returns promptly.
#pragma once

#include "core/AppRuleStore.h"

#include <windows.h>

#include <atomic>
#include <functional>
#include <set>
#include <string>
#include <thread>

namespace hideutil {
namespace core {

class ProcessWatcher {
public:
    using Callback = std::function<void(DWORD pid, const std::wstring& processName)>;

    ProcessWatcher();
    ~ProcessWatcher();

    ProcessWatcher(const ProcessWatcher&) = delete;
    ProcessWatcher& operator=(const ProcessWatcher&) = delete;

    // The rule store is read by the worker thread; it must outlive the watcher
    // (the orchestrator owns both). PIDs already matching at Start() time are
    // considered "known" and do NOT trigger the callback.
    bool Start(const AppRuleStore& store, int pollIntervalMs, Callback onNewTarget);
    void Stop();
    bool IsRunning() const { return running_.load(); }

private:
    void ThreadMain();

    const AppRuleStore* store_ = nullptr;
    int intervalMs_ = 1500;
    Callback callback_;
    std::thread thread_;
    HANDLE stopEvent_ = nullptr;
    std::atomic<bool> running_{false};
    std::set<DWORD> active_;
};

}  // namespace core
}  // namespace hideutil
