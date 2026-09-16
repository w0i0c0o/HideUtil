#include "core/VirtualDesktopMover.h"

#include <objbase.h>

#include <cstring>

namespace hideutil {
namespace core {

namespace {

struct DesktopEnumContext {
    IVirtualDesktopManager* manager = nullptr;
    std::vector<GUID>* out = nullptr;
};

BOOL CALLBACK EnumDesktopWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<DesktopEnumContext*>(lParam);
    GUID id{};
    if (SUCCEEDED(ctx->manager->GetWindowDesktopId(hwnd, &id))) {
        bool exists = false;
        for (const GUID& known : *ctx->out) {
            if (std::memcmp(&known, &id, sizeof(GUID)) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            ctx->out->push_back(id);
        }
    }
    return TRUE;
}

}  // namespace

VirtualDesktopMover::VirtualDesktopMover() = default;

VirtualDesktopMover::~VirtualDesktopMover() {
    if (manager_ != nullptr) {
        manager_->Release();
        manager_ = nullptr;
    }
}

bool VirtualDesktopMover::Initialize() {
    if (manager_ != nullptr) {
        return true;
    }
    IVirtualDesktopManager* manager = nullptr;
    const HRESULT hr = CoCreateInstance(CLSID_VirtualDesktopManager, nullptr, CLSCTX_ALL,
                                        IID_PPV_ARGS(&manager));
    if (FAILED(hr) || manager == nullptr) {
        return false;
    }
    manager_ = manager;
    return true;
}

bool VirtualDesktopMover::GuidEqual(REFGUID a, REFGUID b) {
    return std::memcmp(&a, &b, sizeof(GUID)) == 0;
}

std::wstring VirtualDesktopMover::GuidToString(REFGUID guid) {
    wchar_t buffer[64] = {};
    const int length = StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer)));
    if (length <= 0) {
        return std::wstring();
    }
    return std::wstring(buffer, static_cast<std::size_t>(length - 1));
}

bool VirtualDesktopMover::StringToGuid(const std::wstring& text, GUID& guid) {
    if (text.empty()) {
        return false;
    }
    return SUCCEEDED(CLSIDFromString(text.c_str(), &guid));
}

std::vector<GUID> VirtualDesktopMover::DiscoverDesktops() {
    std::vector<GUID> desktops;
    if (!Initialize()) {
        return desktops;
    }
    DesktopEnumContext ctx;
    ctx.manager = manager_;
    ctx.out = &desktops;
    EnumWindows(&EnumDesktopWindowsProc, reinterpret_cast<LPARAM>(&ctx));
    return desktops;
}

bool VirtualDesktopMover::MoveToOtherDesktop(HWND hwnd, std::wstring& prevDesktopId) {
    if (!Initialize() || hwnd == nullptr) {
        return false;
    }
    GUID current{};
    if (FAILED(manager_->GetWindowDesktopId(hwnd, &current))) {
        return false;
    }
    const std::vector<GUID> desktops = DiscoverDesktops();
    for (const GUID& candidate : desktops) {
        if (GuidEqual(candidate, current)) {
            continue;
        }
        if (SUCCEEDED(manager_->MoveWindowToDesktop(hwnd, candidate))) {
            prevDesktopId = GuidToString(current);
            return true;
        }
    }
    // Only the current desktop exists (or the move was rejected): unsupported.
    return false;
}

bool VirtualDesktopMover::RestoreToDesktop(HWND hwnd, const std::wstring& desktopId) {
    if (!Initialize() || hwnd == nullptr) {
        return false;
    }
    GUID target{};
    if (!StringToGuid(desktopId, target)) {
        return false;
    }
    return SUCCEEDED(manager_->MoveWindowToDesktop(hwnd, target));
}

}  // namespace core
}  // namespace hideutil
