// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

#include "core/TrayIconHider.h"

#include <commctrl.h>

#include <algorithm>

namespace hideutil {
namespace core {

namespace {

#ifndef TB_HIDEBUTTON
#define TB_HIDEBUTTON (WM_USER + 4)
#endif

// Minimal prefix of explorer's internal TRAYDATA structure. We only need the
// owner HWND at offset 0.
struct RemoteTrayDataPrefix {
    HWND hwnd;
};

class RemoteToolbarBuffer {
public:
    RemoteToolbarBuffer(HANDLE process, SIZE_T size)
        : process_(process),
          memory_(VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE,
                                 PAGE_READWRITE)) {}

    ~RemoteToolbarBuffer() {
        if (memory_ != nullptr) {
            VirtualFreeEx(process_, memory_, 0, MEM_RELEASE);
        }
    }

    RemoteToolbarBuffer(const RemoteToolbarBuffer&) = delete;
    RemoteToolbarBuffer& operator=(const RemoteToolbarBuffer&) = delete;

    LPVOID get() const { return memory_; }
    bool valid() const { return memory_ != nullptr; }

private:
    HANDLE process_;
    LPVOID memory_;
};

}  // namespace

HWND TrayIconHider::FindTrayToolbar() {
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (tray == nullptr) return nullptr;
    HWND notify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr);
    if (notify == nullptr) return nullptr;
    HWND pager = FindWindowExW(notify, nullptr, L"SysPager", nullptr);
    if (pager == nullptr) return nullptr;
    HWND toolbar = FindWindowExW(pager, nullptr, L"ToolbarWindow32", nullptr);
    return toolbar;
}

bool TrayIconHider::IsSupported() {
    return FindTrayToolbar() != nullptr;
}

bool TrayIconHider::ForEachTrayButton(
    const std::function<bool(int, HWND, DWORD, int)>& fn) {
    HWND toolbar = FindTrayToolbar();
    if (toolbar == nullptr) {
        // Windows 11 (XAML notification area) or an unexpected layout.
        return false;
    }

    DWORD explorerPid = 0;
    GetWindowThreadProcessId(toolbar, &explorerPid);
    if (explorerPid == 0) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
                                     PROCESS_QUERY_INFORMATION,
                                 FALSE, explorerPid);
    if (process == nullptr) {
        return false;
    }

    const int buttonCount = static_cast<int>(SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0));
    RemoteToolbarBuffer remoteButton(process, sizeof(TBBUTTON) + 8);
    RemoteToolbarBuffer remoteTray(process, sizeof(RemoteTrayDataPrefix) + 8);
    if (remoteButton.valid() && remoteTray.valid()) {
        for (int i = 0; i < buttonCount; ++i) {
            TBBUTTON button{};
            if (!SendMessageW(toolbar, TB_GETBUTTON, static_cast<WPARAM>(i),
                              reinterpret_cast<LPARAM>(remoteButton.get()))) {
                continue;
            }
            SIZE_T read = 0;
            if (!ReadProcessMemory(process, remoteButton.get(), &button, sizeof(button), &read) ||
                read != sizeof(button)) {
                continue;
            }
            if (button.dwData == 0) {
                continue;
            }
            RemoteTrayDataPrefix trayData{};
            if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(button.dwData), &trayData,
                                   sizeof(trayData), &read) ||
                read != sizeof(trayData)) {
                continue;
            }
            if (trayData.hwnd == nullptr) {
                continue;
            }
            DWORD ownerPid = 0;
            GetWindowThreadProcessId(trayData.hwnd, &ownerPid);
            if (ownerPid == 0) {
                continue;
            }
            if (!fn(i, trayData.hwnd, ownerPid, button.idCommand)) {
                break;
            }
        }
    }
    CloseHandle(process);
    return true;
}

bool TrayIconHider::ApplyHide(DWORD pid) {
    HWND toolbar = FindTrayToolbar();
    if (toolbar == nullptr) {
        // Windows 11 (XAML notification area) or an unexpected layout: degrade.
        return false;
    }

    bool hidAny = false;
    ForEachTrayButton([&](int index, HWND, DWORD ownerPid, int commandId) {
        if (ownerPid != pid) {
            return true;
        }
        if (SendMessageW(toolbar, TB_HIDEBUTTON, static_cast<WPARAM>(index),
                         MAKELPARAM(TRUE, 0)) != 0) {
            HiddenIcon entry;
            entry.pid = pid;
            entry.toolbar = toolbar;
            entry.commandId = commandId;
            entry.index = index;
            hidden_.push_back(entry);
            hidAny = true;
        }
        return true;
    });
    return hidAny;
}

std::vector<DWORD> TrayIconHider::EnumerateTrayOwnerPids() {
    std::vector<DWORD> result;
    ForEachTrayButton([&](int, HWND, DWORD ownerPid, int) {
        if (std::find(result.begin(), result.end(), ownerPid) == result.end()) {
            result.push_back(ownerPid);
        }
        return true;
    });
    return result;
}

bool TrayIconHider::Restore(DWORD pid) {
    bool restoredAny = false;
    for (auto it = hidden_.begin(); it != hidden_.end();) {
        if (it->pid != pid) {
            ++it;
            continue;
        }
        if (IsWindow(it->toolbar) && it->index >= 0) {
            SendMessageW(it->toolbar, TB_HIDEBUTTON, static_cast<WPARAM>(it->index),
                         MAKELPARAM(FALSE, 0));
            restoredAny = true;
        }
        it = hidden_.erase(it);
    }
    return restoredAny;
}

}  // namespace core
}  // namespace hideutil
