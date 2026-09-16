cmake_minimum_required(VERSION 3.20)
project(HideUtil CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static-link the MSVC C/C++ runtime (/MT) so the deliverable is a single,
# dependency-free executable. This lets the user copy just HideUtil.exe to
# another machine without installing the Visual C++ Redistributable. The
# project has zero third-party deps, so static CRT is safe here.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Core static library: all reusable logic, no entry point.
# ---------------------------------------------------------------------------
add_library(hideutil_core STATIC
    src/core/Json.cpp
    src/core/AppRuleStore.cpp
    src/core/StateStore.cpp
    src/core/ProcessWindowResolver.cpp
    src/core/WindowHider.cpp
    src/core/AudioMuter.cpp
    src/core/WinEventHook.cpp
    src/core/TrayIconHider.cpp
    src/core/TargetEnumerator.cpp
    src/core/HideOrchestrator.cpp
    src/core/ProcessController.cpp
    src/core/ProcessWatcher.cpp
    src/core/VirtualDesktopMover.cpp
    src/core/JumpListCleaner.cpp
    src/ui/SettingsWindow.cpp
    src/ui/IconCache.cpp
    src/self/HotkeyManager.cpp
    src/self/Logger.cpp
    src/self/SelfShell.cpp
)

target_include_directories(hideutil_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(hideutil_core PUBLIC
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

target_compile_options(hideutil_core PRIVATE /utf-8 /W4 /permissive-)

target_link_libraries(hideutil_core PUBLIC
    user32
    ole32
    oleaut32
    comctl32
    shlwapi
    shcore
    shell32
    advapi32
    uuid
)

# ---------------------------------------------------------------------------
# HideUtil: the real GUI (windowless) deliverable, requires elevation.
# ---------------------------------------------------------------------------
add_executable(HideUtil WIN32
    src/main.cpp
    app.manifest
)

target_compile_options(HideUtil PRIVATE /utf-8 /W4 /permissive-)
target_link_libraries(HideUtil PRIVATE hideutil_core)
# Fallback / reinforcement for the requireAdministrator manifest.
target_link_options(HideUtil PRIVATE "/MANIFESTUAC:level='requireAdministrator'")

# ---------------------------------------------------------------------------
# hideutil_cli: console test shell, runs asInvoker (no elevation).
# ---------------------------------------------------------------------------
add_executable(hideutil_cli
    src/cli/main_cli.cpp
)

target_compile_options(hideutil_cli PRIVATE /utf-8 /W4 /permissive-)
target_link_libraries(hideutil_cli PRIVATE hideutil_core)
target_link_options(hideutil_cli PRIVATE "/MANIFESTUAC:level='asInvoker'")

# ---------------------------------------------------------------------------
# hideutil_tests: pure logic unit tests, runs asInvoker (no elevation).
# ---------------------------------------------------------------------------
add_executable(hideutil_tests
    tests/core_tests.cpp
)

target_compile_options(hideutil_tests PRIVATE /utf-8 /W4 /permissive-)
target_link_libraries(hideutil_tests PRIVATE hideutil_core)
target_link_options(hideutil_tests PRIVATE "/MANIFESTUAC:level='asInvoker'")

# ---------------------------------------------------------------------------
# hideutil_qa_tests: [QA] independent UI-reachability test (asInvoker).
# Appended by the QA engineer; adds a new target only and does not change any
# existing target, source file or option.
# ---------------------------------------------------------------------------
add_executable(hideutil_qa_tests
    tests/qa_ui_tests.cpp
)

target_compile_options(hideutil_qa_tests PRIVATE /utf-8 /W4 /permissive-)
target_link_libraries(hideutil_qa_tests PRIVATE hideutil_core)
target_link_options(hideutil_qa_tests PRIVATE "/MANIFESTUAC:level='asInvoker'")
