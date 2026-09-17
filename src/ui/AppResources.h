// Resource identifiers shared between the resource script (resources/HideUtil.rc)
// and the C++ code that loads them.
//
// Single source of truth on purpose: the .rc embeds the icon under this id and
// the UI code loads it back with MAKEINTRESOURCEW(IDI_APP_ICON), so the two
// can never drift apart.
#pragma once

#define IDI_APP_ICON 101
