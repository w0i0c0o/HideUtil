// Process suspend / resume via ntdll's NtSuspendProcess / NtResumeProcess.
//
// ntdll is NEVER statically linked: both functions are resolved at runtime via
// GetProcAddress. Pairing semantics: only processes that THIS tool suspended
// (ProcessSnapshot.wasSuspended == TRUE) may be resumed.
#pragma once

#include "core/Types.h"

#include <windows.h>

namespace hideutil {
namespace core {

class ProcessController {
public:
    ProcessController();
    ~ProcessController();

    ProcessController(const ProcessController&) = delete;
    ProcessController& operator=(const ProcessController&) = delete;

    // True when the ntdll entry points could be resolved.
    bool IsAvailable() const { return ntSuspend_ != nullptr && ntResume_ != nullptr; }

    bool Suspend(DWORD pid);
    bool Resume(DWORD pid);

    // Pure predicate used by the orchestrator (and unit tests) to decide whether
    // a process may be resumed: only processes we suspended ourselves.
    static bool ShouldResume(const ProcessSnapshot& snapshot) {
        return snapshot.wasSuspended != FALSE;
    }

private:
    using NtProcessFn = LONG(NTAPI*)(HANDLE);

    static NtProcessFn Resolve(const char* name);

    NtProcessFn ntSuspend_ = nullptr;
    NtProcessFn ntResume_ = nullptr;
};

}  // namespace core
}  // namespace hideutil
