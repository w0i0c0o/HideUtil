#!/usr/bin/env bash
# Build script for HideUtil (Git Bash on Windows).
#
# The Visual Studio generator could not detect the compiler on the reference
# machine, so we use Ninja with a manually injected MSVC environment, which is
# verified to compile and link the project.
set -euo pipefail

VSB="C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools"
MSVC="$VSB/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"
SDKV="10.0.26100.0"

# TMP / TEMP (link.exe writes lnk{...}.tmp there).
#
# Git Bash sets TMP/TEMP to POSIX paths (/tmp). Native tools cannot resolve
# them, GetTempPath falls back to C:\WINDOWS and linking dies with
#   LINK : fatal error LNK1104: cannot open file 'C:\WINDOWS\lnk{...}.tmp'
# Exporting a Windows-style TMP from bash does NOT reach link.exe either
# (verified: forward slashes, backslashes and MSYS2_ENV_CONV_EXCL all fail).
#
# => When this script is launched from Git Bash, link with a NATIVE shell:
#      powershell: set $env:TMP/$env:TEMP to a Windows path, then
#      cmake --build build
#    Compilation always works; only the LINK step is affected.
export PATH="/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64:/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64:/c/Users/WW/.workbuddy/binaries/python/envs/default/Scripts:$PATH"
export INCLUDE="$MSVC/include;$SDK/Include/$SDKV/ucrt;$SDK/Include/$SDKV/shared;$SDK/Include/$SDKV/um;$SDK/Include/$SDKV/winrt;$SDK/Include/$SDKV/cppwinrt"
export LIB="$MSVC/lib/x64;$SDK/Lib/$SDKV/ucrt/x64;$SDK/Lib/$SDKV/um/x64"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$SCRIPT_DIR"

BUILD_TYPE="${1:-Release}"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build build

echo
echo "Build finished. Outputs:"
ls -la build/HideUtil.exe build/hideutil_cli.exe build/hideutil_tests.exe 2>/dev/null || true
