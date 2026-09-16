// Move windows to another existing virtual desktop using ONLY the documented
// IVirtualDesktopManager interface (no IVirtualDesktopManagerInternal etc.).
//
// The documented API cannot create a new desktop, so we move the target to an
// existing, non-current desktop discovered by scanning other top-level windows.
// When the session has a single desktop we return false and report "unsupported".
#pragma once

#include <windows.h>
#include <shobjidl.h>

#include <string>
#include <vector>

namespace hideutil {
namespace core {

class VirtualDesktopMover {
public:
    VirtualDesktopMover();
    ~VirtualDesktopMover();

    VirtualDesktopMover(const VirtualDesktopMover&) = delete;
    VirtualDesktopMover& operator=(const VirtualDesktopMover&) = delete;

    bool Initialize();
    bool IsAvailable() const { return manager_ != nullptr; }

    // Move hwnd to an existing, different desktop. prevDesktopId receives the
    // GUID string of the window's original desktop (for restore). Returns false
    // when no other desktop exists or the API is unavailable.
    bool MoveToOtherDesktop(HWND hwnd, std::wstring& prevDesktopId);

    bool RestoreToDesktop(HWND hwnd, const std::wstring& desktopId);

    static std::wstring GuidToString(REFGUID guid);
    static bool StringToGuid(const std::wstring& text, GUID& guid);

private:
    std::vector<GUID> DiscoverDesktops();
    static bool GuidEqual(REFGUID a, REFGUID b);

    IVirtualDesktopManager* manager_ = nullptr;
};

}  // namespace core
}  // namespace hideutil
