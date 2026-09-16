// [QA] qahang.exe - adversarial hung-window host (build_qa only).
//
// Creates a real visible top-level window on its main thread and pumps
// messages normally. When it receives WM_APP+0x101 (posted by qactl.exe)
// the window thread suspends ITSELF -- from that moment the window is alive
// but its owning thread no longer pumps messages, exactly like a UWP app
// lifecycle-suspended by the OS (OneNote) or a process frozen by HideUtil's
// suspend action. The thread stays frozen until qactl.exe resumes it via
// OpenThread/ResumeThread.
//
// Usage: qahang.exe
// Prints "QAHANG pid=<pid> tid=<tid>" on stdout (flushed immediately).
#include <windows.h>

#include <cstdio>

namespace {

constexpr wchar_t kClassName[] = L"HideUtilQAHangWnd";
constexpr wchar_t kTitle[] = L"HideUtilQA-HangTarget";
constexpr UINT kMsgHang = WM_APP + 0x101;  // suspend own thread
constexpr UINT kMsgQuit = WM_APP + 0x102;  // destroy window and exit

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == kMsgHang) {
        // Freeze this thread: no more message pumping until an external
        // ResumeThread. GetCurrentThread() is a pseudo handle valid here.
        const DWORD prev = SuspendThread(GetCurrentThread());
        (void)prev;  // resume returns us here with count back to 0
        return 0;
    }
    if (msg == kMsgQuit) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int wmain() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    if (RegisterClassW(&wc) == 0) {
        std::printf("QAHANG error=register_class gl=%lu\n", GetLastError());
        return 1;
    }
    HWND hwnd = CreateWindowExW(0, kClassName, kTitle, WS_OVERLAPPEDWINDOW,
                                120, 120, 360, 240, nullptr, nullptr, instance, nullptr);
    if (hwnd == nullptr) {
        std::printf("QAHANG error=create_window gl=%lu\n", GetLastError());
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    std::printf("QAHANG pid=%lu tid=%lu hwnd=0x%p\n",
                GetCurrentProcessId(), GetCurrentThreadId(), hwnd);
    std::fflush(stdout);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
