#include "ui/IconCache.h"

#include "core/TargetEnumerator.h"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>

namespace hideutil {
namespace ui {

namespace {

// Lower-cases a copy of `text` (ASCII only is enough for paths / process
// names, and avoids pulling in a locale-aware towlower round trip).
std::wstring LowerCopy(const std::wstring& text) {
    std::wstring result = text;
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t ch) {
        if (ch >= L'A' && ch <= L'Z') {
            return static_cast<wchar_t>(ch - L'A' + L'a');
        }
        return ch;
    });
    return result;
}

// Generic executable icon, used whenever a real one cannot be read. Returns
// nullptr when even that fails (the caller then simply shows no icon).
HICON LoadGenericExecutableIcon() {
    SHFILEINFOW info{};
    // SHGFI_USEFILEATTRIBUTES: resolve the icon for a "*.exe" pattern without
    // requiring an actual file to exist.
    if (SHGetFileInfoW(L"*.exe", FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                       SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES) != 0 &&
        info.hIcon != nullptr) {
        return info.hIcon;
    }
    // Last resort: the shell's application icon (shared, must NOT be destroyed).
    return nullptr;
}

}  // namespace

IconCache::IconCache() {
    // 16x16 icons with an alpha channel; grow on demand.
    imageList_ = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 16, 8);
    if (imageList_ == nullptr) {
        return;  // degrade gracefully: no icons, everything else still works
    }
    // Index 0 is the fallback so callers never have to special-case failures.
    HICON generic = LoadGenericExecutableIcon();
    if (generic != nullptr) {
        ImageList_AddIcon(imageList_, generic);
        DestroyIcon(generic);
    } else {
        // SHGetFileInfoW failed too: insert the shared application icon so the
        // reserved slot still exists.
        HICON shared = LoadIconW(nullptr, IDI_APPLICATION);
        if (shared != nullptr) {
            ImageList_AddIcon(imageList_, shared);
            // Shared icon: intentionally NOT destroyed.
        }
    }
}

IconCache::~IconCache() {
    if (imageList_ != nullptr) {
        ImageList_Destroy(imageList_);
        imageList_ = nullptr;
    }
}

int IconCache::IndexFor(const std::wstring& exePath, const std::wstring& displayName) {
    if (imageList_ == nullptr) {
        return kDefaultIconIndex;
    }
    const std::wstring key = IconCacheKey(exePath, displayName);
    const auto existing = indexByKey_.find(key);
    if (existing != indexByKey_.end()) {
        return existing->second;
    }
    const int index = ExtractAndAdd(exePath);
    indexByKey_.emplace(key, index);
    return index;
}

int IconCache::ExtractAndAdd(const std::wstring& exePath) {
    HICON icon = nullptr;

    if (!exePath.empty()) {
        // Preferred route: the shell already knows the associated icon and
        // handles overlay/thumbnail caches for us.
        SHFILEINFOW info{};
        if (SHGetFileInfoW(exePath.c_str(), 0, &info, sizeof(info),
                           SHGFI_ICON | SHGFI_SMALLICON) != 0) {
            icon = info.hIcon;
        }

        // Fallback for files the shell cannot stat (e.g. protected images).
        if (icon == nullptr) {
            HICON small = nullptr;
            ExtractIconExW(exePath.c_str(), 0, nullptr, &small, 1);
            // The return value of ExtractIconExW is ambiguous when only one of
            // the two handle arrays is requested, so rely on the handle.
            if (small != nullptr) {
                icon = small;
            }
        }
    }

    if (icon == nullptr) {
        return kDefaultIconIndex;
    }

    const int added = ImageList_AddIcon(imageList_, icon);
    DestroyIcon(icon);  // ImageList_AddIcon copied the bits.
    return added < 0 ? kDefaultIconIndex : added;
}

IconCache& SharedIconCache() {
    // Function-local static: constructed on first use, destroyed at exit,
    // thread-safe under C++11 magic statics.
    static IconCache cache;
    return cache;
}

std::wstring IconCacheKey(const std::wstring& exePath, const std::wstring& displayName) {
    if (!exePath.empty()) {
        return LowerCopy(exePath);
    }
    return L"name:" + LowerCopy(displayName);
}

}  // namespace ui
}  // namespace hideutil
