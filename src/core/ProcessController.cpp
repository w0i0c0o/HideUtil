// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

#include "core/ProcessController.h"

namespace hideutil {
namespace core {

namespace {

// NTSTATUS >= 0 means success.
bool NtSuccess(LONG status) {
    return status >= 0;
}

}  // namespace

ProcessController::ProcessController() {
    ntSuspend_ = Resolve("NtSuspendProcess");
    ntResume_ = Resolve("NtResumeProcess");
}

ProcessController::~ProcessController() = default;

ProcessController::NtProcessFn ProcessController::Resolve(const char* name) {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return nullptr;
    }
    const FARPROC proc = GetProcAddress(ntdll, name);
    if (proc == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<NtProcessFn>(proc);
}

bool ProcessController::Suspend(DWORD pid) {
    if (ntSuspend_ == nullptr || pid == 0) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_SUSPEND_RESUME | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return false;
    }
    const LONG status = ntSuspend_(process);
    CloseHandle(process);
    return NtSuccess(status);
}

bool ProcessController::Resume(DWORD pid) {
    if (ntResume_ == nullptr || pid == 0) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_SUSPEND_RESUME | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return false;
    }
    const LONG status = ntResume_(process);
    CloseHandle(process);
    return NtSuccess(status);
}

}  // namespace core
}  // namespace hideutil
