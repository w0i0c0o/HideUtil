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
