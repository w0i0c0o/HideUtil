// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

// Small-icon extraction + cache for the settings UI lists.
//
// The picker and the rules list both want one 16x16 icon per row. Extracting
// an icon is comparatively expensive (it touches the file system), so icons
// are cached by executable path and shared between every list through one
// HIMAGELIST.
//
// Ownership: the cache owns the image list (and therefore the copied icons).
// Each extracted HICON is destroyed right after being added, because
// ImageList_AddIcon copies the bits into the image list.
#pragma once

#include <windows.h>

// HIMAGELIST and the ImageList_* helpers are declared in commctrl.h, not
// windows.h. Without this include the header fails to compile and takes the
// entire settings UI translation unit down with it.
#include <commctrl.h>

#include <string>
#include <unordered_map>

namespace hideutil {
namespace ui {

// Fallback index used whenever no icon can be obtained for an entry. Index 0
// is reserved for a generic executable icon inserted at construction time.
constexpr int kDefaultIconIndex = 0;

class IconCache {
public:
    IconCache();
    ~IconCache();

    IconCache(const IconCache&) = delete;
    IconCache& operator=(const IconCache&) = delete;

    // The shared image list. May be nullptr if creation failed, in which case
    // IndexFor() always returns kDefaultIconIndex and callers simply show no
    // icon.
    HIMAGELIST ImageList() const { return imageList_; }

    // Image-list index for the given program. Extracts and caches on first
    // use; `displayName` is only the fallback key when `exePath` is empty.
    int IndexFor(const std::wstring& exePath, const std::wstring& displayName);

private:
    // Extract an icon for this entry and add it to the image list. Returns
    // kDefaultIconIndex when nothing could be extracted.
    int ExtractAndAdd(const std::wstring& exePath);

    HIMAGELIST imageList_ = nullptr;
    std::unordered_map<std::wstring, int> indexByKey_;
};

// Process-wide cache shared by the target picker and the rules list. Icons are
// reused across dialogs and the cache outlives any list view that references
// its image list.
IconCache& SharedIconCache();

// Pure cache-key normalisation: the lower-cased exe path when we have one,
// otherwise a prefixed display name so a name key can never collide with a
// real path key. Exposed so it can be unit tested without Win32 extraction.
std::wstring IconCacheKey(const std::wstring& exePath, const std::wstring& displayName);

}  // namespace ui
}  // namespace hideutil
