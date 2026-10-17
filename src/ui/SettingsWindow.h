// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

// Native Win32 settings window (no third-party UI library).
//
// Created on demand and destroyed on close (WM_CLOSE -> DestroyWindow); the
// main shell never shows a window at startup. Provides:
//   * hotkey recorder for the two global hotkeys (with conflict detection)
//   * rule management (add / edit / delete) over a ListView, where the target
//     program is picked from a searchable list of running programs / windows /
//     tray applications instead of being typed in by hand
//   * Save (persist + apply immediately) / Cancel (discard)
//   * Quit HideUtil (restores everything that was hidden, then exits)
#pragma once

#include "core/AppRuleStore.h"
#include "core/TargetEnumerator.h"
#include "core/Types.h"

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace hideutil {
namespace ui {

// Host callbacks implemented by SelfShell.
struct SettingsHost {
    // Release the global hotkeys while the user is recording (so probing the
    // new combination is not blocked by our own registration).
    std::function<void()> suspendHotkeys;
    // Re-register the global hotkeys from the current configuration.
    std::function<void()> resumeHotkeys;
    // Persist + apply the edited configuration (re-register hotkeys, reload rules).
    std::function<void()> onConfigChanged;
    // Probe a candidate combination on the host window. `usable` is false and
    // `conflict` is set true when the combination is already taken.
    std::function<bool(int slotId, const core::HotkeySpec& spec, bool* conflict)> probeHotkey;
    // Live snapshot of selectable target programs (windows / tray / processes).
    std::function<std::vector<core::TargetCandidate>()> enumerateTargets;
    // Restore everything that was hidden and terminate the process.
    std::function<void()> onQuitApp;
};

class SettingsWindow {
public:
    // Slot identifiers (match HotkeyManager constants 1 / 2).
    static constexpr int kSlotHideToggle = 1;
    static constexpr int kSlotOpenSettings = 2;

    SettingsWindow();
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    static void RegisterWindowClass(HINSTANCE instance);
    static const wchar_t* ClassName();

    // `page` optionally selects which part of the window receives focus:
    // "rules" -> the rule list, "hotkeys" -> the hotkey controls. An empty or
    // unrecognised page degrades to the default focus.
    bool Show(HINSTANCE instance, core::AppRuleStore& store, SettingsHost host,
              const std::wstring& page = std::wstring());
    void Destroy();
    bool IsOpen() const { return hwnd_ != nullptr; }
    HWND Handle() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void LayoutControls();
    void RefreshHotkeyLabels();
    void RefreshRules();
    int SelectedRuleIndex() const;

    // Move the keyboard focus to the control belonging to `page` ("rules" /
    // "hotkeys"). Missing controls degrade silently to the default focus.
    void FocusPage(const std::wstring& page);

    void BeginRecord(int slot);
    bool CaptureHotkey(WPARAM vk);
    void EndRecord(bool accept);

    void OnAddRule();
    void OnEditRule();
    void OnDeleteRule();
    void OnSave();
    void OnQuitApp();

    core::HotkeySpec* SlotSpec(int slot);
    static int SlotFromControlId(int controlId);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;

    HWND labelHotkey_[2] = {};
    HWND valueHotkey_[2] = {};
    HWND buttonRecord_[2] = {};
    HWND checkboxBringToFront_ = nullptr;
    HWND listRules_ = nullptr;
    HWND buttonAdd_ = nullptr;
    HWND buttonEdit_ = nullptr;
    HWND buttonDelete_ = nullptr;
    HWND buttonSave_ = nullptr;
    HWND buttonCancel_ = nullptr;
    HWND buttonQuit_ = nullptr;
    HWND labelStatus_ = nullptr;

    core::AppRuleStore* store_ = nullptr;
    SettingsHost host_;
    core::HotkeySpec working_[2];
    std::vector<core::AppRule> rules_;
    bool bringRestoredToFront_ = false;
    bool dirty_ = false;
    int recordingSlot_ = 0;  // 0 = not recording

    static SettingsWindow* s_instance_;
};

}  // namespace ui
}  // namespace hideutil
