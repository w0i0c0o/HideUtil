// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

#include "core/ProcessWatcher.h"

#include <tlhelp32.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace hideutil {
namespace core {

namespace {

std::vector<std::pair<DWORD, std::wstring>> SnapshotProcesses() {
    std::vector<std::pair<DWORD, std::wstring>> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return result;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            result.emplace_back(entry.th32ProcessID, std::wstring(entry.szExeFile));
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

}  // namespace

ProcessWatcher::ProcessWatcher() = default;

ProcessWatcher::~ProcessWatcher() {
    Stop();
}

bool ProcessWatcher::Start(const AppRuleStore& store, int pollIntervalMs, Callback onNewTarget) {
    Stop();
    store_ = &store;
    intervalMs_ = pollIntervalMs < 100 ? 100 : pollIntervalMs;
    callback_ = std::move(onNewTarget);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent_ == nullptr) {
        return false;
    }

    // Seed the active set with already-matching processes.
    active_.clear();
    for (const auto& proc : SnapshotProcesses()) {
        if (store_->MatchByPid(proc.first) != nullptr) {
            active_.insert(proc.first);
        }
    }

    running_.store(true);
    thread_ = std::thread(&ProcessWatcher::ThreadMain, this);
    return true;
}

void ProcessWatcher::Stop() {
    if (!running_.exchange(false)) {
        if (stopEvent_ != nullptr) {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
        }
        return;
    }
    if (stopEvent_ != nullptr) {
        SetEvent(stopEvent_);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (stopEvent_ != nullptr) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    active_.clear();
    callback_ = nullptr;
}

void ProcessWatcher::ThreadMain() {
    while (running_.load()) {
        const DWORD wait = WaitForSingleObject(stopEvent_, static_cast<DWORD>(intervalMs_));
        if (wait == WAIT_OBJECT_0 || !running_.load()) {
            break;
        }

        std::set<DWORD> matchingNow;
        for (const auto& proc : SnapshotProcesses()) {
            if (!running_.load()) {
                break;
            }
            if (store_ == nullptr) {
                break;
            }
            if (store_->MatchByPid(proc.first) != nullptr) {
                matchingNow.insert(proc.first);
                if (active_.find(proc.first) == active_.end()) {
                    if (callback_) {
                        callback_(proc.first, proc.second);
                    }
                }
            }
        }
        active_ = std::move(matchingNow);
    }
}

}  // namespace core
}  // namespace hideutil
