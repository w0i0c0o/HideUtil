// SPDX-FileCopyrightText: 2026 Xing Wei
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is part of HideUtil, licensed under the GNU General Public
// License v3 or, at your option, any later version. See LICENSE.

// Resource identifiers shared between the resource script (resources/HideUtil.rc)
// and the C++ code that loads them.
//
// Single source of truth on purpose: the .rc embeds the icon under this id and
// the UI code loads it back with MAKEINTRESOURCEW(IDI_APP_ICON), so the two
// can never drift apart.
#pragma once

#define IDI_APP_ICON 101

// Embedded language documents (resources/lang/*.json), loaded by
// i18n/Localizer as RCDATA. Declared here so the resource script and the C++
// loader can never drift apart.
#define IDR_LANG_EN_US 201
#define IDR_LANG_ZH_CN 202
