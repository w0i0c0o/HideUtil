// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

#include "self/Logger.h"

#include <windows.h>

#include <mutex>

namespace hideutil {
namespace self {

namespace {

bool g_fileLoggingEnabled = false;
std::wstring g_filePath;
std::mutex g_mutex;

const wchar_t* LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return L"DEBUG";
        case LogLevel::Info: return L"INFO";
        case LogLevel::Warn: return L"WARN";
        case LogLevel::Error: return L"ERROR";
    }
    return L"INFO";
}

bool FileLoggingEnabled() {
    return g_fileLoggingEnabled && !g_filePath.empty();
}

}  // namespace

bool Logger::enabled_ = false;

void Logger::SetEnabled(bool enabled) {
    enabled_ = enabled;
}

bool Logger::IsEnabled() {
    return enabled_;
}

void Logger::Log(LogLevel level, const std::wstring& message) {
    if (!enabled_) {
        return;
    }
    const std::wstring line = std::wstring(L"[HideUtil][") + LevelName(level) + L"] " + message;

#ifdef _DEBUG
    OutputDebugStringW((line + L"\n").c_str());
#endif

    if (FileLoggingEnabled()) {
        std::lock_guard<std::mutex> lock(g_mutex);
        HANDLE file = CreateFileW(g_filePath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()),
                                              nullptr, 0, nullptr, nullptr);
            if (utf8Len > 0) {
                std::string bytes(static_cast<std::size_t>(utf8Len), '\0');
                WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()),
                                    bytes.data(), utf8Len, nullptr, nullptr);
                bytes.push_back('\n');
                DWORD written = 0;
                WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
            }
            CloseHandle(file);
        }
    }
}

void Logger::Debug(const std::wstring& message) { Log(LogLevel::Debug, message); }
void Logger::Info(const std::wstring& message) { Log(LogLevel::Info, message); }
void Logger::Warn(const std::wstring& message) { Log(LogLevel::Warn, message); }
void Logger::Error(const std::wstring& message) { Log(LogLevel::Error, message); }

bool Logger::EnableFileLogging(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_filePath = path;
    g_fileLoggingEnabled = !path.empty();
    return g_fileLoggingEnabled;
}

void Logger::DisableFileLogging() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_fileLoggingEnabled = false;
    g_filePath.clear();
}

}  // namespace self
}  // namespace hideutil
