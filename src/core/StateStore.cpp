// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

#include "core/StateStore.h"

#include "core/Json.h"

#include <windows.h>
#include <shlobj.h>

#include <chrono>

namespace hideutil {
namespace core {

namespace {

JsonValue PointToJson(const POINT& pt) {
    JsonValue obj = JsonValue::MakeObject();
    obj.Set(L"x", JsonValue::MakeNumber(pt.x));
    obj.Set(L"y", JsonValue::MakeNumber(pt.y));
    return obj;
}

POINT PointFromJson(const JsonValue* value) {
    POINT pt{0, 0};
    if (value && value->IsObject()) {
        if (const JsonValue* x = value->Find(L"x")) pt.x = static_cast<LONG>(x->AsInt());
        if (const JsonValue* y = value->Find(L"y")) pt.y = static_cast<LONG>(y->AsInt());
    }
    return pt;
}

JsonValue RectToJson(const RECT& rc) {
    JsonValue obj = JsonValue::MakeObject();
    obj.Set(L"left", JsonValue::MakeNumber(rc.left));
    obj.Set(L"top", JsonValue::MakeNumber(rc.top));
    obj.Set(L"right", JsonValue::MakeNumber(rc.right));
    obj.Set(L"bottom", JsonValue::MakeNumber(rc.bottom));
    return obj;
}

RECT RectFromJson(const JsonValue* value) {
    RECT rc{0, 0, 0, 0};
    if (value && value->IsObject()) {
        if (const JsonValue* v = value->Find(L"left")) rc.left = static_cast<LONG>(v->AsInt());
        if (const JsonValue* v = value->Find(L"top")) rc.top = static_cast<LONG>(v->AsInt());
        if (const JsonValue* v = value->Find(L"right")) rc.right = static_cast<LONG>(v->AsInt());
        if (const JsonValue* v = value->Find(L"bottom")) rc.bottom = static_cast<LONG>(v->AsInt());
    }
    return rc;
}

JsonValue WindowToJson(const WindowSnapshot& w) {
    JsonValue obj = JsonValue::MakeObject();
    obj.Set(L"hwnd", JsonValue::MakeNumber(static_cast<double>(reinterpret_cast<uintptr_t>(w.hwnd))));
    obj.Set(L"pid", JsonValue::MakeNumber(w.pid));
    obj.Set(L"wasVisible", JsonValue::MakeBool(w.wasVisible != FALSE));
    obj.Set(L"prevStyle", JsonValue::MakeNumber(static_cast<double>(w.prevStyle)));
    obj.Set(L"prevExStyle", JsonValue::MakeNumber(static_cast<double>(w.prevExStyle)));
    obj.Set(L"taskbarTabRemoved", JsonValue::MakeBool(w.taskbarTabRemoved != FALSE));
    obj.Set(L"wasForeground", JsonValue::MakeBool(w.wasForeground != FALSE));
    obj.Set(L"altTabSuppressed", JsonValue::MakeBool(w.altTabSuppressed != FALSE));
    obj.Set(L"movedToDesktop", JsonValue::MakeBool(w.movedToDesktop != FALSE));
    obj.Set(L"prevDesktopId", JsonValue::MakeString(w.prevDesktopId));

    JsonValue placement = JsonValue::MakeObject();
    placement.Set(L"flags", JsonValue::MakeNumber(w.placement.flags));
    placement.Set(L"showCmd", JsonValue::MakeNumber(w.placement.showCmd));
    placement.Set(L"ptMinPosition", PointToJson(w.placement.ptMinPosition));
    placement.Set(L"ptMaxPosition", PointToJson(w.placement.ptMaxPosition));
    placement.Set(L"rcNormalPosition", RectToJson(w.placement.rcNormalPosition));
    obj.Set(L"placement", std::move(placement));
    return obj;
}

WindowSnapshot WindowFromJson(const JsonValue& value) {
    WindowSnapshot w;
    w.placement.length = sizeof(WINDOWPLACEMENT);
    if (const JsonValue* v = value.Find(L"hwnd")) {
        w.hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(v->AsInt()));
    }
    if (const JsonValue* v = value.Find(L"pid")) w.pid = static_cast<DWORD>(v->AsInt());
    if (const JsonValue* v = value.Find(L"wasVisible")) w.wasVisible = v->AsBool() ? TRUE : FALSE;
    if (const JsonValue* v = value.Find(L"prevStyle")) w.prevStyle = static_cast<LONG_PTR>(v->AsInt());
    if (const JsonValue* v = value.Find(L"prevExStyle")) w.prevExStyle = static_cast<LONG_PTR>(v->AsInt());
    if (const JsonValue* v = value.Find(L"taskbarTabRemoved")) w.taskbarTabRemoved = v->AsBool() ? TRUE : FALSE;
    if (const JsonValue* v = value.Find(L"wasForeground")) w.wasForeground = v->AsBool() ? TRUE : FALSE;
    if (const JsonValue* v = value.Find(L"altTabSuppressed")) w.altTabSuppressed = v->AsBool() ? TRUE : FALSE;
    if (const JsonValue* v = value.Find(L"movedToDesktop")) w.movedToDesktop = v->AsBool() ? TRUE : FALSE;
    if (const JsonValue* v = value.Find(L"prevDesktopId")) w.prevDesktopId = v->AsString();
    if (const JsonValue* placement = value.Find(L"placement")) {
        if (const JsonValue* v = placement->Find(L"flags")) w.placement.flags = static_cast<UINT>(v->AsInt());
        if (const JsonValue* v = placement->Find(L"showCmd")) w.placement.showCmd = static_cast<UINT>(v->AsInt());
        w.placement.ptMinPosition = PointFromJson(placement->Find(L"ptMinPosition"));
        w.placement.ptMaxPosition = PointFromJson(placement->Find(L"ptMaxPosition"));
        w.placement.rcNormalPosition = RectFromJson(placement->Find(L"rcNormalPosition"));
    }
    return w;
}

JsonValue AudioToJson(const AudioSnapshot& a) {
    JsonValue obj = JsonValue::MakeObject();
    obj.Set(L"sessionId", JsonValue::MakeString(a.sessionId));
    obj.Set(L"pid", JsonValue::MakeNumber(a.pid));
    obj.Set(L"wasMuted", JsonValue::MakeBool(a.wasMuted != FALSE));
    obj.Set(L"prevVolume", JsonValue::MakeNumber(a.prevVolume));
    return obj;
}

AudioSnapshot AudioFromJson(const JsonValue& value) {
    AudioSnapshot a;
    if (const JsonValue* v = value.Find(L"sessionId")) a.sessionId = v->AsString();
    if (const JsonValue* v = value.Find(L"pid")) a.pid = static_cast<DWORD>(v->AsInt());
    if (const JsonValue* v = value.Find(L"wasMuted")) a.wasMuted = v->AsBool() ? TRUE : FALSE;
    if (const JsonValue* v = value.Find(L"prevVolume")) a.prevVolume = static_cast<float>(v->AsNumber());
    return a;
}

}  // namespace

StateStore::StateStore() = default;

void StateStore::BeginSnapshot() {
    pending_ = GlobalSnapshot{};
    pending_.hiddenAt = std::chrono::system_clock::now();
    begun_ = true;
}

void StateStore::AddWindow(const WindowSnapshot& snapshot) {
    if (!begun_) BeginSnapshot();
    pending_.windows.push_back(snapshot);
}

void StateStore::AddAudio(const AudioSnapshot& snapshot) {
    if (!begun_) BeginSnapshot();
    pending_.audio.push_back(snapshot);
}

void StateStore::AddProcess(const ProcessSnapshot& snapshot) {
    if (!begun_) BeginSnapshot();
    pending_.processes.push_back(snapshot);
}

void StateStore::AddJumpListPid(DWORD pid) {
    if (pid == 0) return;
    if (!begun_) BeginSnapshot();
    for (DWORD existing : pending_.jumpListPids) {
        if (existing == pid) return;
    }
    pending_.jumpListPids.push_back(pid);
}

void StateStore::Commit() {
    committed_ = pending_;
    begun_ = false;
}

void StateStore::Clear() {
    pending_ = GlobalSnapshot{};
    committed_ = GlobalSnapshot{};
    begun_ = false;
}

void StateStore::SetSnapshot(const GlobalSnapshot& snapshot) {
    committed_ = snapshot;
    pending_ = GlobalSnapshot{};
    begun_ = false;
}

GlobalSnapshot StateStore::MergeSnapshots(const GlobalSnapshot& base,
                                          const std::vector<WindowSnapshot>& observedWindows,
                                          const std::vector<AudioSnapshot>& observedAudio,
                                          const std::vector<ProcessSnapshot>& observedProcesses,
                                          const std::vector<DWORD>& observedJumpListPids) {
    // Start from the authoritative base; never drop an existing entry.
    GlobalSnapshot merged = base;

    for (const WindowSnapshot& window : observedWindows) {
        bool seen = false;
        for (const WindowSnapshot& existing : merged.windows) {
            if (existing.hwnd == window.hwnd) {
                seen = true;
                break;
            }
        }
        if (!seen) merged.windows.push_back(window);
    }

    for (const AudioSnapshot& session : observedAudio) {
        bool seen = false;
        for (const AudioSnapshot& existing : merged.audio) {
            if (existing.sessionId == session.sessionId) {
                seen = true;
                break;
            }
        }
        if (!seen) merged.audio.push_back(session);
    }

    for (const ProcessSnapshot& process : observedProcesses) {
        bool seen = false;
        for (const ProcessSnapshot& existing : merged.processes) {
            if (existing.pid == process.pid) {
                seen = true;
                break;
            }
        }
        if (!seen) merged.processes.push_back(process);
    }

    for (DWORD pid : observedJumpListPids) {
        if (pid == 0) continue;
        bool seen = false;
        for (DWORD existing : merged.jumpListPids) {
            if (existing == pid) {
                seen = true;
                break;
            }
        }
        if (!seen) merged.jumpListPids.push_back(pid);
    }

    return merged;
}

GlobalSnapshot StateStore::PruneStaleWindows(const GlobalSnapshot& base,
                                             const std::vector<WindowRef>& observed) {
    GlobalSnapshot pruned;
    // Non-window collections are not the subject of this pruning.
    pruned.hiddenAt = base.hiddenAt;
    pruned.moveToVirtualDesktopApplied = base.moveToVirtualDesktopApplied;
    pruned.audio = base.audio;
    pruned.processes = base.processes;
    pruned.jumpListPids = base.jumpListPids;

    for (const WindowSnapshot& window : base.windows) {
        const WindowRef* match = nullptr;
        for (const WindowRef& ref : observed) {
            if (ref.hwnd == window.hwnd) {
                match = &ref;
                break;
            }
        }

        if (match == nullptr) {
            // Not observed this round: might belong to another hide batch, so it
            // must be preserved for restore. Never delete on absence.
            pruned.windows.push_back(window);
            continue;
        }
        if (match->pid == window.pid) {
            // Same HWND and same PID: the current correct entry. Keep verbatim.
            pruned.windows.push_back(window);
            continue;
        }
        // Same HWND but a different PID: the handle was recycled by another
        // process, so this entry is stale and unsafe to restore. Drop it; the
        // caller re-snapshots the recycled window as a brand-new one.
    }

    return pruned;
}

bool StateStore::Persist(const std::wstring& path) const {
    // Make sure the parent directory exists before writing.
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        const std::wstring dir = path.substr(0, slash);
        CreateDirectoryW(dir.c_str(), nullptr);
    }

    JsonValue root = JsonValue::MakeObject();
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         committed_.hiddenAt.time_since_epoch())
                         .count();
    root.Set(L"hiddenAt", JsonValue::MakeNumber(static_cast<double>(now)));
    root.Set(L"moveToVirtualDesktopApplied", JsonValue::MakeBool(committed_.moveToVirtualDesktopApplied != FALSE));

    JsonValue windows = JsonValue::MakeArray();
    for (const auto& w : committed_.windows) windows.Push(WindowToJson(w));
    root.Set(L"windows", std::move(windows));

    JsonValue audio = JsonValue::MakeArray();
    for (const auto& a : committed_.audio) audio.Push(AudioToJson(a));
    root.Set(L"audio", std::move(audio));

    JsonValue processes = JsonValue::MakeArray();
    for (const auto& p : committed_.processes) {
        JsonValue obj = JsonValue::MakeObject();
        obj.Set(L"pid", JsonValue::MakeNumber(p.pid));
        obj.Set(L"wasSuspended", JsonValue::MakeBool(p.wasSuspended != FALSE));
        processes.Push(std::move(obj));
    }
    root.Set(L"processes", std::move(processes));

    JsonValue jumpListPids = JsonValue::MakeArray();
    for (DWORD pid : committed_.jumpListPids) {
        jumpListPids.Push(JsonValue::MakeNumber(pid));
    }
    root.Set(L"jumpListPids", std::move(jumpListPids));

    return WriteTextFileUtf8(path, root.Dump(2));
}

bool StateStore::RestoreFromFile(const std::wstring& path) {
    std::wstring text;
    if (!ReadTextFileUtf8(path, text)) {
        return false;
    }
    JsonValue root;
    std::wstring error;
    if (!JsonValue::Parse(text, root, error) || !root.IsObject()) {
        return false;
    }
    GlobalSnapshot snapshot;
    if (const JsonValue* v = root.Find(L"hiddenAt")) {
        snapshot.hiddenAt = std::chrono::system_clock::time_point(
            std::chrono::seconds(static_cast<long long>(v->AsNumber())));
    }
    if (const JsonValue* v = root.Find(L"moveToVirtualDesktopApplied")) {
        snapshot.moveToVirtualDesktopApplied = v->AsBool() ? TRUE : FALSE;
    }
    if (const JsonValue* windows = root.Find(L"windows")) {
        for (std::size_t i = 0; i < windows->Size(); ++i) {
            snapshot.windows.push_back(WindowFromJson((*windows)[i]));
        }
    }
    if (const JsonValue* audio = root.Find(L"audio")) {
        for (std::size_t i = 0; i < audio->Size(); ++i) {
            snapshot.audio.push_back(AudioFromJson((*audio)[i]));
        }
    }
    if (const JsonValue* processes = root.Find(L"processes")) {
        for (std::size_t i = 0; i < processes->Size(); ++i) {
            const JsonValue& item = (*processes)[i];
            ProcessSnapshot p;
            if (const JsonValue* v = item.Find(L"pid")) p.pid = static_cast<DWORD>(v->AsInt());
            if (const JsonValue* v = item.Find(L"wasSuspended")) p.wasSuspended = v->AsBool() ? TRUE : FALSE;
            snapshot.processes.push_back(p);
        }
    }
    if (const JsonValue* jumpListPids = root.Find(L"jumpListPids")) {
        for (std::size_t i = 0; i < jumpListPids->Size(); ++i) {
            snapshot.jumpListPids.push_back(static_cast<DWORD>((*jumpListPids)[i].AsInt()));
        }
    }
    committed_ = snapshot;
    begun_ = false;
    return true;
}

std::wstring StateStore::DefaultStatePath() {
    PWSTR appData = nullptr;
    std::wstring base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)) && appData) {
        base.assign(appData);
        CoTaskMemFree(appData);
    }
    if (base.empty()) {
        return L"state.json";
    }
    return base + L"\\HideUtil\\state.json";
}

}  // namespace core
}  // namespace hideutil
