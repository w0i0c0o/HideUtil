// [QA] Independent UI-reachability test for SettingsWindow.
//
// Proves, without elevation and without modifying any existing source:
//   * the settings window class registers successfully, and
//   * a real top-level window can be created via CreateWindowExW and destroyed.
//
// Deliberately does NOT call ShowWindow() (no focus stealing) and does NOT call
// CreateControls() (keeps the test headless and side-effect free).
#include "ui/SettingsWindow.h"

#include <windows.h>

#include <commctrl.h>
#include <objbase.h>

#include <cstdio>
#include <cwchar>

using hideutil::ui::SettingsWindow;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* name) {
    ++g_checks;
    if (condition) {
        std::printf("[PASS] %s\n", name);
    } else {
        ++g_failures;
        std::printf("[FAIL] %s (GetLastError=%lu)\n", name, GetLastError());
    }
}

}  // namespace

int wmain() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const bool comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    std::printf("==== hideutil_qa_tests (SettingsWindow reachability) ====\n");

    const wchar_t* className = SettingsWindow::ClassName();
    Check(className != nullptr && className[0] != L'\0', "SettingsWindow::ClassName() non-empty");

    // 1) Register the settings window class. Requires no elevation.
    SettingsWindow::RegisterWindowClass(instance);

    WNDCLASSEXW info{};
    info.cbSize = sizeof(info);
    const bool classRegistered =
        GetClassInfoExW(instance, className, &info) != FALSE;
    Check(classRegistered, "SettingsWindow class registered (GetClassInfoExW)");

    // 2) Create the window directly. No ShowWindow -> the window must stay hidden.
    HWND hwnd = CreateWindowExW(0, className, L"HideUtil QA settings",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 0, 0, 580, 420,
                                nullptr, nullptr, instance, nullptr);
    Check(hwnd != nullptr && IsWindow(hwnd) != FALSE,
          "SettingsWindow created via CreateWindowExW");
    Check(hwnd != nullptr && IsWindowVisible(hwnd) == FALSE,
          "created window stays hidden (no ShowWindow called)");

    // 3) The created window must report the expected class name.
    wchar_t cls[64] = {};
    if (hwnd != nullptr) {
        GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
    }
    Check(hwnd != nullptr && std::wcscmp(cls, className) == 0,
          "GetClassNameW matches SettingsWindow::ClassName()");

    // 4) Destroy cleanly and confirm the handle is gone.
    if (hwnd != nullptr) {
        DestroyWindow(hwnd);
    }
    Check(hwnd != nullptr && IsWindow(hwnd) == FALSE, "window destroyed cleanly");

    if (comInitialized) {
        CoUninitialize();
    }

    std::printf("========================\n");
    std::printf("checks=%d failures=%d -> %s\n", g_checks, g_failures,
                g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
