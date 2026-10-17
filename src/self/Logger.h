// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

// Minimal logger. Disabled by default (no file, no trace). Only debug builds
// emit to OutputDebugString, and only when explicitly enabled.
#pragma once

#include <string>

namespace hideutil {
namespace self {

enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    static void SetEnabled(bool enabled);
    static bool IsEnabled();

    static void Log(LogLevel level, const std::wstring& message);
    static void Debug(const std::wstring& message);
    static void Info(const std::wstring& message);
    static void Warn(const std::wstring& message);
    static void Error(const std::wstring& message);

    // Optional file mirror; off by default. Returns false on failure.
    static bool EnableFileLogging(const std::wstring& path);
    static void DisableFileLogging();

private:
    static bool enabled_;
};

}  // namespace self
}  // namespace hideutil
