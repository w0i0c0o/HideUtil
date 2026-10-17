#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Xing Wei
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This file is part of HideUtil, licensed under the GNU General Public
# License v3 or, at your option, any later version. See LICENSE.
# [QA] build_qa/build_qa.sh - compiles the QA helper executables into build_qa/bin.
# Mirrors the MSVC environment injection of scripts/build.sh but NEVER touches
# build/ or src/ -- the only artifact it reads from build/ is hideutil_core.lib
# (the product under test).
set -euo pipefail

VSB="C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools"
MSVC="$VSB/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"
SDKV="10.0.26100.0"

export PATH="/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64:/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64:$PATH"
export INCLUDE="$MSVC/include;$SDK/Include/$SDKV/ucrt;$SDK/Include/$SDKV/shared;$SDK/Include/$SDKV/um;$SDK/Include/$SDKV/winrt;$SDK/Include/$SDKV/cppwinrt"
export LIB="$MSVC/lib/x64;$SDK/Lib/$SDKV/ucrt/x64;$SDK/Lib/$SDKV/um/x64"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(cygpath -w "$ROOT/build_qa/bin")"
SRC="$(cygpath -w "$ROOT/build_qa/src")"
CORELIB="$(cygpath -w "$ROOT/build/hideutil_core.lib")"
INCSRC="$(cygpath -w "$ROOT/src")"
mkdir -p "$ROOT/build_qa/bin"

# Keep MSYS from mangling cl.exe's slash-style options.
export MSYS2_ARG_CONV_EXCL='*'
CLFLAGS="/utf-8 /W4 /permissive- /EHsc /O2 /MD /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0A00 /std:c++20 /Fo$OUT\\"

# qahang / qactl: standalone, no project dependency.
cl.exe $CLFLAGS "$SRC\\qahang.cpp" /link /SUBSYSTEM:CONSOLE "/OUT:$OUT\\qahang.exe" user32.lib gdi32.lib
cl.exe $CLFLAGS "$SRC\\qactl.cpp" /link /SUBSYSTEM:CONSOLE "/OUT:$OUT\\qactl.exe" user32.lib gdi32.lib

# qadriver: drives the real core library (the code under test).
cl.exe $CLFLAGS "/I$INCSRC" "$SRC\\qadriver.cpp" \
    /link /SUBSYSTEM:CONSOLE "/OUT:$OUT\\qadriver.exe" \
    "$CORELIB" \
    user32.lib ole32.lib oleaut32.lib comctl32.lib shlwapi.lib shcore.lib shell32.lib advapi32.lib uuid.lib

echo
echo "QA helpers built:"
ls -la "$ROOT/build_qa/bin"
