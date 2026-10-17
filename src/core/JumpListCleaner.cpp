// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

#include "core/JumpListCleaner.h"

#include <appmodel.h>
#include <shobjidl.h>

#include <vector>

namespace hideutil {
namespace core {

bool JumpListCleaner::TryGetAppUserModelId(DWORD pid, std::wstring& appId) {
    if (pid == 0) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return false;
    }
    UINT32 length = 0;
    LONG status = GetApplicationUserModelId(process, &length, nullptr);
    if (status != ERROR_INSUFFICIENT_BUFFER || length == 0) {
        CloseHandle(process);
        return false;
    }
    std::vector<wchar_t> buffer(length, L'\0');
    status = GetApplicationUserModelId(process, &length, buffer.data());
    CloseHandle(process);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    appId.assign(buffer.data());
    return !appId.empty();
}

bool JumpListCleaner::ClearForAppId(const std::wstring& appUserModelId) {
    if (appUserModelId.empty()) {
        return false;
    }
    IApplicationDestinations* destinations = nullptr;
    const HRESULT createHr = CoCreateInstance(CLSID_ApplicationDestinations, nullptr,
                                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&destinations));
    if (FAILED(createHr) || destinations == nullptr) {
        return false;
    }
    HRESULT hr = destinations->SetAppID(appUserModelId.c_str());
    if (SUCCEEDED(hr)) {
        hr = destinations->RemoveAllDestinations();
    }
    destinations->Release();
    return SUCCEEDED(hr);
}

bool JumpListCleaner::ClearForProcess(DWORD pid) {
    std::wstring appId;
    if (!TryGetAppUserModelId(pid, appId)) {
        return false;  // no AppUserModelID -> unsupported, do nothing
    }
    return ClearForAppId(appId);
}

}  // namespace core
}  // namespace hideutil
