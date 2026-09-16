// [QA] qactl.exe - control / inspection helper for the QA regression suite
// (build_qa only). Subcommands:
//
//   qactl hang                    post WM_APP+0x101 to the qahang window
//                                (its owning thread suspends itself)
//   qactl resume <tid>            OpenThread(THREAD_SUSPEND_RESUME) + ResumeThread
//   qactl rect <title>            print hwnd / visibility / rect / styles of the
//                                top-level window with the exact title
//   qactl rectbypid <pid>         same, for every top-level window of <pid>
//   qactl kill <pid>              TerminateProcess (simulates a force kill)
//   qactl spawn <exe> [args...]   start a detached process, print its pid
#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>

namespace {

constexpr wchar_t kHangTitle[] = L"HideUtilQA-HangTarget";
constexpr UINT kMsgHang = WM_APP + 0x101;

void PrintWindowInfo(HWND hwnd) {
    RECT r{};
    GetWindowRect(hwnd, &r);
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    wchar_t title[128] = {};
    GetWindowTextW(hwnd, title, 128);
    std::printf("hwnd=0x%p pid=%lu visible=%d rect=%ld,%ld,%ld,%ld size=%ldx%ld "
                "style=0x%llx exStyle=0x%llx title='%ls'\n",
                hwnd, pid, IsWindowVisible(hwnd) ? 1 : 0,
                r.left, r.top, r.right, r.bottom,
                r.right - r.left, r.bottom - r.top,
                static_cast<unsigned long long>(style),
                static_cast<unsigned long long>(exStyle), title);
}

struct EnumCtx {
    DWORD pid = 0;
    int found = 0;
};

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx->pid && GetWindow(hwnd, GW_OWNER) == nullptr) {
        ++ctx->found;
        PrintWindowInfo(hwnd);
    }
    return TRUE;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: qactl hang | resume <tid> | rect <title> | "
                    "rectbypid <pid> | kill <pid> | spawn <exe> [args...]\n");
        return 1;
    }
    const std::wstring mode = argv[1];

    if (mode == L"hang") {
        const HWND hwnd = FindWindowW(nullptr, kHangTitle);
        if (hwnd == nullptr) {
            std::printf("hang: window not found\n");
            return 2;
        }
        const BOOL ok = PostMessageW(hwnd, kMsgHang, 0, 0);
        std::printf("hang: posted=%d hwnd=0x%p\n", ok ? 1 : 0, hwnd);
        return ok ? 0 : 3;
    }

    if (mode == L"resume") {
        if (argc < 3) { std::printf("resume: tid required\n"); return 1; }
        const DWORD tid = static_cast<DWORD>(std::wcstoul(argv[2], nullptr, 0));
        const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, tid);
        if (thread == nullptr) {
            std::printf("resume: OpenThread failed gl=%lu\n", GetLastError());
            return 2;
        }
        const DWORD prev = ResumeThread(thread);
        CloseHandle(thread);
        // prev == 0 means it was NOT suspended; 1 means one pending suspension
        // was released (exactly what we expect from a self-suspended thread).
        std::printf("resume: tid=%lu prevSuspendCount=%lu\n", tid, prev);
        return (prev == 0 || prev == 0xFFFFFFFF) ? 3 : 0;
    }

    if (mode == L"rect") {
        if (argc < 3) { std::printf("rect: title required\n"); return 1; }
        const HWND hwnd = FindWindowW(nullptr, argv[2]);
        if (hwnd == nullptr) {
            std::printf("rect: window not found\n");
            return 2;
        }
        PrintWindowInfo(hwnd);
        return 0;
    }

    if (mode == L"rectbypid") {
        if (argc < 3) { std::printf("rectbypid: pid required\n"); return 1; }
        EnumCtx ctx;
        ctx.pid = static_cast<DWORD>(std::wcstoul(argv[2], nullptr, 0));
        EnumWindows(&EnumProc, reinterpret_cast<LPARAM>(&ctx));
        std::printf("rectbypid: pid=%lu topLevelWindows=%d\n", ctx.pid, ctx.found);
        return ctx.found > 0 ? 0 : 2;
    }

    if (mode == L"kill") {
        if (argc < 3) { std::printf("kill: pid required\n"); return 1; }
        const DWORD pid = static_cast<DWORD>(std::wcstoul(argv[2], nullptr, 0));
        const HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (process == nullptr) {
            std::printf("kill: OpenProcess failed gl=%lu\n", GetLastError());
            return 2;
        }
        const BOOL ok = TerminateProcess(process, 1);
        if (ok) WaitForSingleObject(process, 5000);
        CloseHandle(process);
        std::printf("kill: pid=%lu terminated=%d\n", pid, ok ? 1 : 0);
        return ok ? 0 : 3;
    }

    if (mode == L"spawn") {
        if (argc < 3) { std::printf("spawn: exe required\n"); return 1; }
        std::wstring command = argv[2];
        for (int i = 3; i < argc; ++i) {
            command += L" ";
            command += argv[i];
        }
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        wchar_t* mutableCmd = &command[0];
        const BOOL ok = CreateProcessW(nullptr, mutableCmd, nullptr, nullptr, FALSE,
                                       CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi);
        if (!ok) {
            std::printf("spawn: CreateProcess failed gl=%lu\n", GetLastError());
            return 2;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        std::printf("spawn: pid=%lu cmd='%ls'\n", pi.dwProcessId, command.c_str());
        return 0;
    }

    std::printf("qactl: unknown mode '%ls'\n", mode.c_str());
    return 1;
}
