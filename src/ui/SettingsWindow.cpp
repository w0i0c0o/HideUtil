#include "ui/SettingsWindow.h"

#include "ui/IconCache.h"

#include "self/Logger.h"

#include <commctrl.h>

#include <algorithm>
#include <cwchar>

namespace hideutil {
namespace ui {

namespace {

// Group ids for the target picker list (grouped view requires comctl32 v6).
enum : int {
    kGroupHasWindow = 0,
    kGroupBackground = 1,
};

// --- Windows 11 look helpers ------------------------------------------------

// Segoe UI 9pt is the Windows 11 default UI font. Returns nullptr on failure
// (callers then simply leave the system default font in place).
HFONT CreateUiFont(int points, bool semiBold) {
    HDC screen = GetDC(nullptr);
    const int pixels = screen != nullptr
                           ? -MulDiv(points, GetDeviceCaps(screen, LOGPIXELSY), 72)
                           : -points * 96 / 72;
    if (screen != nullptr) {
        ReleaseDC(nullptr, screen);
    }
    return CreateFontW(pixels, 0, 0, 0, semiBold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

// Process-wide UI font. Controls reference the font for their whole lifetime,
// so it must outlive them; a single static handle is the simplest safe
// lifetime for a single-instance app (one handle, released by the OS at exit).
HFONT UiFont() {
    static HFONT font = CreateUiFont(9, false);
    return font;
}

// Apply the UI font to a control and every descendant (recursive helper, so
// newly added child controls are covered automatically).
void SetFontRecursive(HWND hwnd, HFONT font) {
    if (hwnd == nullptr || font == nullptr) {
        return;
    }
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    for (HWND child = GetWindow(hwnd, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        SetFontRecursive(child, font);
    }
}

// The user's accent colour, used for the primary button highlight. Falls back
// to the system selection colour when the DWM value is unavailable.
COLORREF AccentColor() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_READ,
                      &key) == ERROR_SUCCESS) {
        DWORD value = 0;
        DWORD size = sizeof(value);
        const LONG status = RegQueryValueExW(key, L"AccentColor", nullptr, nullptr,
                                             reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(key);
        if (status == ERROR_SUCCESS) {
            // AccentColor is stored as 0xAABBGGRR; COLORREF is 0x00BBGGRR.
            const BYTE red = static_cast<BYTE>(value & 0xFF);
            const BYTE green = static_cast<BYTE>((value >> 8) & 0xFF);
            const BYTE blue = static_cast<BYTE>((value >> 16) & 0xFF);
            return RGB(red, green, blue);
        }
    }
    return GetSysColor(COLOR_HIGHLIGHT);
}

// Windows 11 list style: full-row selection, double buffered (no flicker) and
// explicitly no grid lines. LVS_SHAREIMAGELISTS is essential because the icon
// image list is owned by the shared cache -- without it the list view would
// destroy ours when the window closes.
void ApplyModernListStyle(HWND list) {
    if (list == nullptr) {
        return;
    }
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetImageList(list, SharedIconCache().ImageList(), LVSIL_SMALL);
}

// (Re)creates the "has window" / "background" groups with their member counts.
// Only non-empty groups are inserted so the list never shows an empty header.
void ApplyPickerGroups(HWND list, int withWindowCount, int backgroundCount) {
    if (list == nullptr) {
        return;
    }
    ListView_RemoveAllGroups(list);

    const auto insert = [list](int groupId, const std::wstring& header) {
        LVGROUP group{};
        group.cbSize = sizeof(group);
        group.mask = LVGF_GROUPID | LVGF_HEADER | LVGF_ALIGN | LVGF_STATE;
        group.iGroupId = groupId;
        group.uAlign = LVGA_HEADER_LEFT;
        group.state = LVGS_NORMAL;
        // InsertGroup copies the text, so a temporary buffer is fine.
        std::vector<wchar_t> text(header.begin(), header.end());
        text.push_back(L'\0');
        group.pszHeader = text.data();
        ListView_InsertGroup(list, -1, &group);
    };

    if (withWindowCount > 0) {
        insert(kGroupHasWindow,
               L"有窗口的应用程序（" + std::to_wstring(withWindowCount) + L"）· 建议优先选择");
    }
    if (backgroundCount > 0) {
        insert(kGroupBackground,
               L"后台进程 · 无窗口（" + std::to_wstring(backgroundCount) + L"）");
    }
}

// Text colour for a row: background (no visible window) entries are
// de-emphasised so the user's eye goes to the programs that actually have a
// window to hide.
COLORREF RowTextColor(const core::TargetCandidate& candidate) {
    return candidate.hasVisibleWindow ? GetSysColor(COLOR_WINDOWTEXT)
                                      : GetSysColor(COLOR_GRAYTEXT);
}

enum : int {
    IDC_LABEL_HOTKEY0 = 1001,
    IDC_VALUE_HOTKEY0 = 1002,
    IDC_RECORD0 = 1003,
    IDC_LABEL_HOTKEY1 = 1011,
    IDC_VALUE_HOTKEY1 = 1012,
    IDC_RECORD1 = 1013,
    IDC_BRING_TO_FRONT = 1014,
    IDC_LIST = 1100,
    IDC_ADD = 1200,
    IDC_EDIT = 1201,
    IDC_DELETE = 1202,
    IDC_SAVE = 1300,
    IDC_CANCEL = 1301,
    IDC_QUIT = 1302,
    IDC_STATUS = 1400,
};

// Rule editor control ids.
enum : int {
    IDC_RE_NAME = 4001,
    IDC_RE_EXEPATH = 4002,
    IDC_RE_PROC = 4003,
    IDC_RE_CLASS = 4004,
    IDC_RE_MODE = 4005,
    IDC_RE_HIDEWIN = 4010,
    IDC_RE_HIDETRAY = 4011,
    IDC_RE_MUTE = 4012,
    IDC_RE_TOOLWIN = 4013,
    IDC_RE_SUSPEND = 4014,
    IDC_RE_DESKTOP = 4015,
    IDC_RE_JUMPLIST = 4016,
    IDC_RE_PICK = 4022,
    IDC_RE_OK = 4020,
    IDC_RE_CANCEL = 4021,
};

// Target picker control ids.
enum : int {
    IDC_TP_FILTER = 5000,
    IDC_TP_REFRESH = 5001,
    IDC_TP_LIST = 5002,
    IDC_TP_OK = 5003,
    IDC_TP_CANCEL = 5004,
    IDC_TP_STATUS = 5005,
};

const wchar_t* kRuleEditorClass = L"HideUtilRuleEditor";
const wchar_t* kTargetPickerClass = L"HideUtilTargetPicker";

std::wstring VkToName(UINT vk) {
    if (vk >= VK_F1 && vk <= VK_F24) {
        wchar_t buffer[8];
        swprintf_s(buffer, L"F%u", vk - VK_F1 + 1);
        return buffer;
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    switch (vk) {
        case VK_SPACE: return L"SPACE";
        case VK_TAB: return L"TAB";
        case VK_RETURN: return L"ENTER";
        case VK_BACK: return L"BACKSPACE";
        case VK_DELETE: return L"DELETE";
        case VK_INSERT: return L"INSERT";
        case VK_HOME: return L"HOME";
        case VK_END: return L"END";
        case VK_PRIOR: return L"PGUP";
        case VK_NEXT: return L"PGDN";
        case VK_UP: return L"UP";
        case VK_DOWN: return L"DOWN";
        case VK_LEFT: return L"LEFT";
        case VK_RIGHT: return L"RIGHT";
        case VK_PAUSE: return L"PAUSE";
        case VK_SNAPSHOT: return L"PRINTSCREEN";
        case VK_CAPITAL: return L"CAPSLOCK";
        default: break;
    }
    wchar_t buffer[16];
    swprintf_s(buffer, L"VK_0x%02X", vk);
    return buffer;
}

const wchar_t* ModifierName(UINT flag) {
    switch (flag) {
        case MOD_CONTROL: return L"CTRL";
        case MOD_ALT: return L"ALT";
        case MOD_SHIFT: return L"SHIFT";
        case MOD_WIN: return L"WIN";
        default: return L"";
    }
}

std::wstring FormatSpec(const core::HotkeySpec& spec) {
    std::wstring result;
    for (const std::wstring& modifier : spec.modifiers) {
        result += modifier;
        result += L"+";
    }
    result += spec.key.empty() ? L"(未设置)" : spec.key;
    return result;
}

core::HotkeySpec MakeSpec(UINT mods, UINT vk) {
    core::HotkeySpec spec;
    if (mods & MOD_CONTROL) spec.modifiers.push_back(L"CTRL");
    if (mods & MOD_ALT) spec.modifiers.push_back(L"ALT");
    if (mods & MOD_SHIFT) spec.modifiers.push_back(L"SHIFT");
    if (mods & MOD_WIN) spec.modifiers.push_back(L"WIN");
    spec.key = VkToName(vk);
    spec.modFlags = mods;
    spec.vk = vk;
    return spec;
}

bool IsPureModifierKey(WPARAM vk) {
    switch (vk) {
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
            return true;
        default:
            return false;
    }
}

std::wstring GetWindowTextString(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return std::wstring();
    }
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

std::wstring DescribeMatch(const core::AppRule& rule) {
    std::wstring result;
    if (rule.match.exePath) result += L"exe=" + *rule.match.exePath + L" ";
    if (rule.match.processName) result += L"proc=" + *rule.match.processName + L" ";
    if (rule.match.windowClass) result += L"class=" + *rule.match.windowClass + L" ";
    result += (rule.match.matchMode == core::MatchMode::All) ? L"[all]" : L"[any]";
    return result;
}

std::wstring DescribeActions(const core::AppRule& rule) {
    std::wstring result;
    if (rule.actions.hideWindow) result += L"hide ";
    if (rule.actions.hideTrayIcon) result += L"tray ";
    if (rule.actions.muteAudio) result += L"mute ";
    if (rule.actions.hideFromAltTab) result += L"alttab ";
    if (rule.actions.suspendProcess) result += L"suspend ";
    if (rule.actions.moveToVirtualDesktop) result += L"vdesk ";
    if (rule.actions.clearJumpList) result += L"jumplist ";
    return result;
}

// ---------------------------------------------------------------------------
// Rule editor (simple modal dialog built from stock controls).
// ---------------------------------------------------------------------------

// Defined further down; declared here because the rule editor opens it.
bool RunTargetPickerDialog(HINSTANCE instance, HWND parent,
                           const std::function<std::vector<core::TargetCandidate>()>& enumerate,
                           core::TargetCandidate& picked);

struct RuleEditorState {
    HWND hwnd = nullptr;
    HWND parent = nullptr;
    HINSTANCE instance = nullptr;
    core::AppRule rule;
    const std::function<std::vector<core::TargetCandidate>()>* enumerate = nullptr;
    bool accepted = false;
};

void SetControlText(HWND parent, int id, const std::wstring& text) {
    SetDlgItemTextW(parent, id, text.c_str());
}

std::wstring ControlText(HWND parent, int id) {
    return GetWindowTextString(GetDlgItem(parent, id));
}

bool IsChecked(HWND parent, int id) {
    return SendDlgItemMessageW(parent, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

LRESULT CALLBACK RuleEditorProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<RuleEditorState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDC_RE_PICK && state != nullptr && state->enumerate != nullptr) {
                core::TargetCandidate picked;
                if (RunTargetPickerDialog(state->instance, hwnd, *state->enumerate, picked)) {
                    if (!picked.exePath.empty()) SetControlText(hwnd, IDC_RE_EXEPATH, picked.exePath);
                    if (!picked.processName.empty()) SetControlText(hwnd, IDC_RE_PROC, picked.processName);
                    if (!picked.windowClass.empty()) SetControlText(hwnd, IDC_RE_CLASS, picked.windowClass);
                    const std::wstring currentName = ControlText(hwnd, IDC_RE_NAME);
                    if (currentName.empty() || currentName == L"新规则") {
                        SetControlText(hwnd, IDC_RE_NAME, picked.processName);
                    }
                }
                return 0;
            }
            if (id == IDC_RE_OK && state != nullptr) {
                const std::wstring name = ControlText(hwnd, IDC_RE_NAME);
                const std::wstring exePath = ControlText(hwnd, IDC_RE_EXEPATH);
                const std::wstring proc = ControlText(hwnd, IDC_RE_PROC);
                const std::wstring cls = ControlText(hwnd, IDC_RE_CLASS);
                if (exePath.empty() && proc.empty() && cls.empty()) {
                    MessageBoxW(hwnd, L"至少需要填写 exe 路径 / 进程名 / 窗口类 之一。", L"提示",
                                MB_OK | MB_ICONWARNING);
                    return 0;
                }
                core::AppRule rule;
                rule.id = state->rule.id;
                rule.enabled = state->rule.enabled;
                rule.name = name;
                if (!exePath.empty()) rule.match.exePath = exePath;
                if (!proc.empty()) rule.match.processName = proc;
                if (!cls.empty()) rule.match.windowClass = cls;
                const LRESULT modeIndex = SendDlgItemMessageW(hwnd, IDC_RE_MODE, CB_GETCURSEL, 0, 0);
                rule.match.matchMode = (modeIndex == 1) ? core::MatchMode::All : core::MatchMode::Any;
                rule.actions.hideWindow = IsChecked(hwnd, IDC_RE_HIDEWIN);
                rule.actions.hideTrayIcon = IsChecked(hwnd, IDC_RE_HIDETRAY);
                rule.actions.muteAudio = IsChecked(hwnd, IDC_RE_MUTE);
                rule.actions.hideFromAltTab = IsChecked(hwnd, IDC_RE_TOOLWIN);
                rule.actions.suspendProcess = IsChecked(hwnd, IDC_RE_SUSPEND);
                rule.actions.moveToVirtualDesktop = IsChecked(hwnd, IDC_RE_DESKTOP);
                rule.actions.clearJumpList = IsChecked(hwnd, IDC_RE_JUMPLIST);
                rule.restore = state->rule.restore;
                state->rule = rule;
                state->accepted = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == IDC_RE_CANCEL) {
                if (state != nullptr) state->accepted = false;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            if (state != nullptr) state->accepted = false;
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void RegisterRuleEditorClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &RuleEditorProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kRuleEditorClass;
    RegisterClassExW(&wc);
    registered = true;
}

void AddLabel(HWND parent, const wchar_t* text, int x, int y, int w) {
    CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, 20, parent,
                    nullptr, nullptr, nullptr);
}

void AddEdit(HWND parent, int id, int x, int y, int w, const std::wstring& value) {
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, x, y, w, 24,
                                parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr,
                                nullptr);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

void AddCheck(HWND parent, int id, const wchar_t* text, int x, int y, int w, bool checked) {
    HWND box = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                               x, y, w, 22, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               nullptr, nullptr);
    SendMessageW(box, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageW(box, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

// ---------------------------------------------------------------------------
// Target picker: a searchable list of running programs, their visible windows
// and their tray icons. Replaces hand-typing an executable path.
// ---------------------------------------------------------------------------

struct TargetPickerState {
    HWND hwnd = nullptr;
    HWND parent = nullptr;
    HWND list = nullptr;
    std::vector<core::TargetCandidate> all;
    std::vector<core::TargetCandidate> visible;
    const std::function<std::vector<core::TargetCandidate>()>* enumerate = nullptr;
    core::TargetCandidate picked;
    bool accepted = false;
};

void FillPickerList(TargetPickerState& state) {
    if (state.list == nullptr || state.hwnd == nullptr) {
        return;
    }
    ListView_DeleteAllItems(state.list);

    // Build the filtered collection FIRST so that NM_CUSTOMDRAW (which fires
    // during painting, and possibly while items are being inserted) can always
    // map a row index back to its candidate.
    const std::wstring query = core::ToLowerCopy(ControlText(state.hwnd, IDC_TP_FILTER));
    state.visible.clear();
    for (const core::TargetCandidate& candidate : state.all) {
        if (core::MatchesFilter(candidate, query)) {
            state.visible.push_back(candidate);
        }
    }

    // Group counts, then group headers with the counts in them.
    int withWindowCount = 0;
    for (const core::TargetCandidate& candidate : state.visible) {
        if (candidate.hasVisibleWindow) {
            ++withWindowCount;
        }
    }
    const int backgroundCount = static_cast<int>(state.visible.size()) -
                                static_cast<int>(withWindowCount);
    ApplyPickerGroups(state.list, withWindowCount, backgroundCount);

    // Insert the rows: icon + group assignment + the usual columns.
    IconCache& icons = SharedIconCache();
    for (std::size_t i = 0; i < state.visible.size(); ++i) {
        const core::TargetCandidate& candidate = state.visible[i];
        std::wstring name = candidate.processName.empty() ? L"(未知程序)" : candidate.processName;

        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_GROUPID;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<LPWSTR>(name.c_str());
        item.iImage = icons.IndexFor(candidate.exePath, candidate.processName);
        item.iGroupId = candidate.hasVisibleWindow ? kGroupHasWindow : kGroupBackground;
        const int inserted = ListView_InsertItem(state.list, &item);

        const std::wstring badges = core::CandidateBadges(candidate);
        ListView_SetItemText(state.list, inserted, 1, const_cast<LPWSTR>(badges.c_str()));
        const std::wstring title = candidate.windowTitle.empty() ? L"-" : candidate.windowTitle;
        ListView_SetItemText(state.list, inserted, 2, const_cast<LPWSTR>(title.c_str()));
        const std::wstring path = candidate.exePath.empty() ? L"-" : candidate.exePath;
        ListView_SetItemText(state.list, inserted, 3, const_cast<LPWSTR>(path.c_str()));
    }

    if (!state.visible.empty()) {
        ListView_SetItemState(state.list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
    wchar_t summary[160];
    swprintf_s(summary, L"共 %zu 个程序，当前显示 %zu 个（有窗口 %d，无窗口 %d）。选中后点「确定」。",
               state.all.size(), state.visible.size(), withWindowCount, backgroundCount);
    SetDlgItemTextW(state.hwnd, IDC_TP_STATUS, summary);
}

void AcceptPickerSelection(TargetPickerState& state) {
    const int row = ListView_GetNextItem(state.list, -1, LVNI_SELECTED);
    if (row < 0 || static_cast<std::size_t>(row) >= state.visible.size()) {
        MessageBoxW(state.hwnd, L"请先在上方列表中选择一个程序。", L"提示",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    state.picked = state.visible[static_cast<std::size_t>(row)];
    state.accepted = true;
    DestroyWindow(state.hwnd);
}

LRESULT CALLBACK TargetPickerProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<TargetPickerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int notify = HIWORD(wParam);
            if (state == nullptr) {
                break;
            }
            if (id == IDC_TP_FILTER && notify == EN_CHANGE) {
                FillPickerList(*state);
                return 0;
            }
            if (id == IDC_TP_REFRESH) {
                if (state->enumerate != nullptr) {
                    state->all = (*state->enumerate)();
                }
                FillPickerList(*state);
                return 0;
            }
            if (id == IDC_TP_OK) {
                AcceptPickerSelection(*state);
                return 0;
            }
            if (id == IDC_TP_CANCEL) {
                state->accepted = false;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<LPNMHDR>(lParam);
            if (state != nullptr && header != nullptr && header->code == NM_DBLCLK &&
                header->idFrom == IDC_TP_LIST) {
                AcceptPickerSelection(*state);
                return 0;
            }
            // De-emphasise rows that have no visible window so the user's eye
            // goes straight to the programs they can actually see (and hide).
            if (state != nullptr && header != nullptr && header->code == NM_CUSTOMDRAW &&
                header->idFrom == IDC_TP_LIST) {
                auto* draw = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);
                switch (draw->nmcd.dwDrawStage) {
                    case CDDS_PREPAINT:
                        return CDRF_NOTIFYITEMDRAW;
                    case CDDS_ITEMPREPAINT: {
                        const DWORD row = static_cast<DWORD>(draw->nmcd.dwItemSpec);
                        if (row < state->visible.size()) {
                            draw->clrText = RowTextColor(state->visible[row]);
                            return CDRF_NEWFONT;
                        }
                        return CDRF_DODEFAULT;
                    }
                    default:
                        break;
                }
            }
            break;
        }
        case WM_CLOSE:
            if (state != nullptr) {
                state->accepted = false;
            }
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void RegisterTargetPickerClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TargetPickerProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kTargetPickerClass;
    RegisterClassExW(&wc);
    registered = true;
}

bool RunTargetPickerDialog(HINSTANCE instance, HWND parent,
                           const std::function<std::vector<core::TargetCandidate>()>& enumerate,
                           core::TargetCandidate& picked) {
    RegisterTargetPickerClass(instance);

    TargetPickerState state;
    state.parent = parent;
    state.enumerate = &enumerate;
    state.all = enumerate ? enumerate() : std::vector<core::TargetCandidate>();

    RECT rect{0, 0, 724, 470};
    AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kTargetPickerClass,
                                L"选择要隐藏的程序", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                                rect.bottom - rect.top, parent, nullptr, instance, nullptr);
    if (hwnd == nullptr) {
        return false;
    }
    state.hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    // Windows 11: Segoe UI 9pt everywhere, applied recursively so any control
    // added later is covered too.
    SetFontRecursive(hwnd, UiFont());

    const HMENU filterMenu = reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TP_FILTER));
    HWND filter = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 16, 16, 502, 26,
                                  hwnd, filterMenu, instance, nullptr);
    HWND refresh = CreateWindowExW(0, L"BUTTON", L"刷新", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 526, 16,
                                   88, 26, hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TP_REFRESH)),
                                   instance, nullptr);

    HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
                                    LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
                                16, 50, 692, 330, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TP_LIST)), instance,
                                nullptr);
    // No grid lines (Windows 11 lists are flat), double buffered, and the
    // shared icon image list attached.
    ApplyModernListStyle(list);
    // Grouped view: "has window" vs "background / no window".
    ListView_EnableGroupView(list, TRUE);
    struct ColumnSpec {
        const wchar_t* title;
        int width;
    };
    // First column a little wider to make room for the 16x16 icon.
    const ColumnSpec columns[] = {{L"程序", 196}, {L"标记", 96}, {L"窗口标题", 196}, {L"可执行路径", 180}};
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.cx = columns[i].width;
        column.pszText = const_cast<LPWSTR>(columns[i].title);
        ListView_InsertColumn(list, i, &column);
    }

    HWND status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 16, 388, 420, 22,
                                  hwnd,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TP_STATUS)),
                                  instance, nullptr);

    HWND ok = CreateWindowExW(0, L"BUTTON", L"确定",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 516, 392, 90, 30,
                              hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TP_OK)), instance,
                              nullptr);
    HWND cancel = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 614, 392,
                                  90, 30, hwnd,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TP_CANCEL)),
                                  instance, nullptr);

    // Fonts for everything created after the recursive pass above.
    HWND all[] = {filter, refresh, list, status, ok, cancel};
    for (HWND control : all) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont()), TRUE);
    }

    state.list = list;
    FillPickerList(state);

    RECT parentRect{}, windowRect{};
    GetWindowRect(parent, &parentRect);
    GetWindowRect(hwnd, &windowRect);
    SetWindowPos(hwnd, nullptr,
                 parentRect.left + ((parentRect.right - parentRect.left) - (windowRect.right - windowRect.left)) / 2,
                 parentRect.top + ((parentRect.bottom - parentRect.top) - (windowRect.bottom - windowRect.top)) / 2,
                 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    EnableWindow(parent, FALSE);
    SetForegroundWindow(hwnd);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);

    if (state.accepted) {
        picked = state.picked;
    }
    return state.accepted;
}

bool RunRuleEditorDialog(HINSTANCE instance, HWND parent,
                         const std::function<std::vector<core::TargetCandidate>()>& enumerateTargets,
                         core::AppRule& rule) {
    RegisterRuleEditorClass(instance);

    RuleEditorState state;
    state.parent = parent;
    state.instance = instance;
    state.rule = rule;
    state.enumerate = &enumerateTargets;

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kRuleEditorClass,
                                L"规则编辑", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 580, 430, parent, nullptr, instance,
                                nullptr);
    if (hwnd == nullptr) {
        return false;
    }
    state.hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    AddLabel(hwnd, L"名称", 15, 15, 100);
    AddEdit(hwnd, IDC_RE_NAME, 120, 12, 320, rule.name);
    AddLabel(hwnd, L"exe 路径", 15, 47, 100);
    AddEdit(hwnd, IDC_RE_EXEPATH, 120, 44, 320, rule.match.exePath ? *rule.match.exePath : L"");
    HWND pick = CreateWindowExW(0, L"BUTTON", L"从列表选择…", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 450,
                                44, 110, 26, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RE_PICK)), nullptr,
                                nullptr);
    SendMessageW(pick, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    AddLabel(hwnd, L"进程名", 15, 79, 100);
    AddEdit(hwnd, IDC_RE_PROC, 120, 76, 320, rule.match.processName ? *rule.match.processName : L"");
    AddLabel(hwnd, L"窗口类", 15, 111, 100);
    AddEdit(hwnd, IDC_RE_CLASS, 120, 108, 320, rule.match.windowClass ? *rule.match.windowClass : L"");

    AddLabel(hwnd, L"匹配模式", 15, 143, 100);
    HWND combo = CreateWindowExW(0, L"COMBOBOX", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 120, 141, 160,
                                 200, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RE_MODE)),
                                 nullptr, nullptr);
    SendMessageW(combo, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"any"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"all"));
    SendMessageW(combo, CB_SETCURSEL,
                 (rule.match.matchMode == core::MatchMode::All) ? 1 : 0, 0);

    AddCheck(hwnd, IDC_RE_HIDEWIN, L"隐藏窗口", 15, 180, 140, rule.actions.hideWindow);
    AddCheck(hwnd, IDC_RE_HIDETRAY, L"隐藏托盘图标", 165, 180, 140, rule.actions.hideTrayIcon);
    AddCheck(hwnd, IDC_RE_MUTE, L"静音", 315, 180, 120, rule.actions.muteAudio);
    AddCheck(hwnd, IDC_RE_TOOLWIN, L"移出 Alt+Tab", 15, 206, 140, rule.actions.hideFromAltTab);
    AddCheck(hwnd, IDC_RE_SUSPEND, L"挂起进程", 165, 206, 140, rule.actions.suspendProcess);
    AddCheck(hwnd, IDC_RE_DESKTOP, L"移到独立虚拟桌面", 15, 232, 180,
             rule.actions.moveToVirtualDesktop);
    AddCheck(hwnd, IDC_RE_JUMPLIST, L"恢复时清空跳转列表", 200, 232, 200,
             rule.actions.clearJumpList);

    const HMENU okMenu = reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RE_OK));
    const HMENU cancelMenu = reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RE_CANCEL));
    HWND ok = CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                              370, 340, 90, 30, hwnd, okMenu, nullptr, nullptr);
    HWND cancel = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  470, 340, 90, 30, hwnd, cancelMenu, nullptr, nullptr);
    SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // Center on parent.
    RECT parentRect{}, rect{};
    GetWindowRect(parent, &parentRect);
    GetWindowRect(hwnd, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    SetWindowPos(hwnd, nullptr,
                 parentRect.left + ((parentRect.right - parentRect.left) - width) / 2,
                 parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2,
                 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    EnableWindow(parent, FALSE);
    SetForegroundWindow(hwnd);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);

    if (state.accepted) {
        rule = state.rule;
    }
    return state.accepted;
}

}  // namespace

// ---------------------------------------------------------------------------
// SettingsWindow.
// ---------------------------------------------------------------------------

SettingsWindow* SettingsWindow::s_instance_ = nullptr;

SettingsWindow::SettingsWindow() = default;
SettingsWindow::~SettingsWindow() = default;

const wchar_t* SettingsWindow::ClassName() { return L"HideUtilSettingsWindow"; }

void SettingsWindow::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = ClassName();
    RegisterClassExW(&wc);
}

int SettingsWindow::SlotFromControlId(int controlId) {
    if (controlId == IDC_RECORD0) return kSlotHideToggle;
    if (controlId == IDC_RECORD1) return kSlotOpenSettings;
    return 0;
}

core::HotkeySpec* SettingsWindow::SlotSpec(int slot) {
    if (slot == kSlotHideToggle) return &working_[0];
    if (slot == kSlotOpenSettings) return &working_[1];
    return nullptr;
}

namespace {

// Move a window that is not visible on any monitor (off-screen, or on a
// monitor that has been unplugged) back onto the primary work area.
void CenterOnPrimaryMonitor(HWND hwnd) {
    const HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return;
    }
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        return;
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int left = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
    const int top = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
    SetWindowPos(hwnd, nullptr, left, top, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
}

// A. Bring an already-open settings window back under the user's cursor.
//
// SetForegroundWindow alone is not enough: the window may be minimised, buried
// behind other windows, or parked on a monitor that no longer exists -- in
// every one of those cases the user sees "nothing happened", which is exactly
// the silent-failure bug this sequence fixes.
void ActivateExistingWindow(HWND hwnd) {
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }
    ShowWindow(hwnd, SW_SHOW);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    // Off-screen / vanished monitor: without this the window is technically
    // "shown" but cannot be seen anywhere.
    if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL) == nullptr) {
        CenterOnPrimaryMonitor(hwnd);
    }
    if (!SetForegroundWindow(hwnd)) {
        // The foreground request was denied (foreground lockout). There is no
        // tray icon by design, so the taskbar button is the only visible
        // anchor -- flash it until the user comes back to it.
        FLASHWINFO flash{};
        flash.cbSize = sizeof(flash);
        flash.hwnd = hwnd;
        flash.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
        flash.uCount = 0;
        flash.dwTimeout = 0;
        FlashWindowEx(&flash);
    }
}

}  // namespace

bool SettingsWindow::Show(HINSTANCE instance, core::AppRuleStore& store, SettingsHost host,
                          const std::wstring& page) {
    if (hwnd_ != nullptr) {
        ActivateExistingWindow(hwnd_);
        FocusPage(page);
        return true;
    }
    s_instance_ = this;
    instance_ = instance;
    store_ = &store;
    host_ = std::move(host);
    recordingSlot_ = 0;
    dirty_ = false;

    working_[0] = store.Config().hotkey.hideToggle;
    working_[1] = store.Config().hotkey.openSettings;
    rules_ = store.Config().rules;
    bringRestoredToFront_ = store.Config().bringRestoredToFront;

    RegisterWindowClass(instance);

    RECT rect{0, 0, 660, 460};
    AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
    hwnd_ = CreateWindowExW(0, ClassName(), L"HideUtil 设置",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            rect.right - rect.left, rect.bottom - rect.top,
                            nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        s_instance_ = nullptr;
        return false;
    }

    // Create the controls and lay them out.
    CreateControls();
    RefreshHotkeyLabels();
    RefreshRules();
    LayoutControls();

    // Segoe UI 9pt on the window and every descendant (recursive, so controls
    // added later are covered too).
    SetFontRecursive(hwnd_, UiFont());

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    SetForegroundWindow(hwnd_);
    FocusPage(page);
    return true;
}

void SettingsWindow::FocusPage(const std::wstring& page) {
    if (page.empty()) {
        return;  // no page requested: keep the default focus
    }
    HWND target = nullptr;
    if (page == L"rules") {
        target = listRules_;
    } else if (page == L"hotkeys") {
        // The "record" button is the actionable hotkey control; only fall back
        // to the value label if the button is somehow missing.
        target = (buttonRecord_[0] != nullptr) ? buttonRecord_[0] : valueHotkey_[0];
    }
    if (target == nullptr || !IsWindow(target)) {
        return;  // degrade silently: never fail a wake-up over focus
    }
    SetFocus(target);
}

void SettingsWindow::CreateControls() {
    const HFONT font = UiFont();  // Segoe UI 9pt (Windows 11 default)
    const DWORD staticStyle = WS_CHILD | WS_VISIBLE | SS_LEFT;

    labelHotkey_[0] = CreateWindowExW(0, L"STATIC", L"一键隐藏 / 恢复：", staticStyle, 0, 0, 1, 1,
                                      hwnd_, nullptr, instance_, nullptr);
    valueHotkey_[0] = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                      0, 0, 1, 1, hwnd_, nullptr, instance_, nullptr);
    buttonRecord_[0] = CreateWindowExW(0, L"BUTTON", L"录制",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1, 1, hwnd_,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RECORD0)),
                                       instance_, nullptr);

    labelHotkey_[1] = CreateWindowExW(0, L"STATIC", L"打开设置界面：", staticStyle, 0, 0, 1, 1, hwnd_,
                                      nullptr, instance_, nullptr);
    valueHotkey_[1] = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                      0, 0, 1, 1, hwnd_, nullptr, instance_, nullptr);
    buttonRecord_[1] = CreateWindowExW(0, L"BUTTON", L"录制",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1, 1, hwnd_,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RECORD1)),
                                       instance_, nullptr);

    checkboxBringToFront_ = CreateWindowExW(
        0, L"BUTTON", L"恢复时将窗口置顶（不勾选则保持当前桌面层级，避免抢占前台）",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 1, 1, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BRING_TO_FRONT)),
        instance_, nullptr);
    SendMessageW(checkboxBringToFront_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageW(checkboxBringToFront_, BM_SETCHECK,
                 bringRestoredToFront_ ? BST_CHECKED : BST_UNCHECKED, 0);

    listRules_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
                                     LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
                                 0, 0, 1, 1, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST)), instance_,
                                 nullptr);
    // Windows 11 list: no grid lines, double buffered, shared icon list.
    ApplyModernListStyle(listRules_);
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.cx = 178;
    column.pszText = const_cast<LPWSTR>(L"名称");
    ListView_InsertColumn(listRules_, 0, &column);
    column.cx = 236;
    column.pszText = const_cast<LPWSTR>(L"匹配");
    ListView_InsertColumn(listRules_, 1, &column);
    column.cx = 156;
    column.pszText = const_cast<LPWSTR>(L"动作");
    ListView_InsertColumn(listRules_, 2, &column);

    buttonAdd_ = CreateWindowExW(0, L"BUTTON", L"新增", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1, 1,
                                 hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ADD)),
                                 instance_, nullptr);
    buttonEdit_ = CreateWindowExW(0, L"BUTTON", L"编辑", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1,
                                  1, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT)),
                                  instance_, nullptr);
    buttonDelete_ = CreateWindowExW(0, L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0,
                                     1, 1, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DELETE)),
                                     instance_, nullptr);
    buttonSave_ = CreateWindowExW(0, L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1,
                                  1, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SAVE)),
                                  instance_, nullptr);
    buttonCancel_ = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0,
                                    1, 1, hwnd_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CANCEL)),
                                    instance_, nullptr);
    buttonQuit_ = CreateWindowExW(0, L"BUTTON", L"退出 HideUtil", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,
                                  0, 1, 1, hwnd_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QUIT)), instance_,
                                  nullptr);
    labelStatus_ = CreateWindowExW(0, L"STATIC", L"", staticStyle, 0, 0, 1, 1, hwnd_, nullptr,
                                   instance_, nullptr);

    HWND controls[] = {labelHotkey_[0], valueHotkey_[0], buttonRecord_[0], labelHotkey_[1],
                       valueHotkey_[1], buttonRecord_[1], checkboxBringToFront_, listRules_,
                       buttonAdd_,      buttonEdit_,     buttonDelete_,   buttonSave_,
                       buttonCancel_,   buttonQuit_,     labelStatus_};
    for (HWND control : controls) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void SettingsWindow::LayoutControls() {
    // Windows 11 spacing: 16px window margin, 8px grid between controls.
    const int margin = 16;
    const int gap = 8;
    const int rowHeight = 26;
    const int contentWidth = 624;

    const int labelWidth = 128;
    const int valueX = margin + labelWidth + gap;
    const int recordX = valueX + 216 + gap;

    MoveWindow(labelHotkey_[0], margin, margin, labelWidth, 22, TRUE);
    MoveWindow(valueHotkey_[0], valueX, margin, 216, 22, TRUE);
    MoveWindow(buttonRecord_[0], recordX, margin, 80, 24, TRUE);

    MoveWindow(labelHotkey_[1], margin, margin + rowHeight, labelWidth, 22, TRUE);
    MoveWindow(valueHotkey_[1], valueX, margin + rowHeight, 216, 22, TRUE);
    MoveWindow(buttonRecord_[1], recordX, margin + rowHeight, 80, 24, TRUE);

    // Global restore behaviour checkbox lives on its own row between the
    // hotkey area and the rule list.
    const int checkboxY = margin + rowHeight * 2 + gap;
    MoveWindow(checkboxBringToFront_, margin, checkboxY, contentWidth, 22, TRUE);

    const int listY = checkboxY + rowHeight + gap;
    MoveWindow(listRules_, margin, listY, contentWidth, 200, TRUE);

    const int buttonsY = listY + 200 + 12;
    MoveWindow(buttonAdd_, margin, buttonsY, 80, 28, TRUE);
    MoveWindow(buttonEdit_, margin + 80 + gap, buttonsY, 80, 28, TRUE);
    MoveWindow(buttonDelete_, margin + (80 + gap) * 2, buttonsY, 80, 28, TRUE);
    MoveWindow(buttonQuit_, margin + (80 + gap) * 2 + 80 + 16, buttonsY, 130, 28, TRUE);
    // Right-aligned pair: 保存 then 取消.
    MoveWindow(buttonCancel_, margin + contentWidth - 80, buttonsY, 80, 28, TRUE);
    MoveWindow(buttonSave_, margin + contentWidth - 80 - gap - 80, buttonsY, 80, 28, TRUE);

    MoveWindow(labelStatus_, margin, buttonsY + 28 + 12, contentWidth, 22, TRUE);
}

void SettingsWindow::RefreshHotkeyLabels() {
    if (valueHotkey_[0] != nullptr) {
        SetWindowTextW(valueHotkey_[0],
                       recordingSlot_ == kSlotHideToggle ? L"按下组合键… (Esc 取消)"
                                                         : FormatSpec(working_[0]).c_str());
    }
    if (valueHotkey_[1] != nullptr) {
        SetWindowTextW(valueHotkey_[1],
                       recordingSlot_ == kSlotOpenSettings ? L"按下组合键… (Esc 取消)"
                                                           : FormatSpec(working_[1]).c_str());
    }
}

void SettingsWindow::RefreshRules() {
    if (listRules_ == nullptr) {
        return;
    }
    ListView_DeleteAllItems(listRules_);
    IconCache& icons = SharedIconCache();
    for (std::size_t i = 0; i < rules_.size(); ++i) {
        const core::AppRule& rule = rules_[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_IMAGE;
        item.iItem = static_cast<int>(i);
        std::wstring name = rule.name.empty() ? rule.id : rule.name;
        if (!rule.enabled) name += L" (禁用)";
        item.pszText = const_cast<LPWSTR>(name.c_str());
        // Show the program's own icon so the selected app is recognisable at a
        // glance, exactly like in the picker. Falls back to the generic exe
        // icon when the rule has no exe path recorded.
        const std::wstring exePath = rule.match.exePath ? *rule.match.exePath : std::wstring();
        item.iImage = icons.IndexFor(exePath, rule.name);
        const int row = ListView_InsertItem(listRules_, &item);
        std::wstring match = DescribeMatch(rule);
        ListView_SetItemText(listRules_, row, 1, const_cast<LPWSTR>(match.c_str()));
        std::wstring actions = DescribeActions(rule);
        ListView_SetItemText(listRules_, row, 2, const_cast<LPWSTR>(actions.c_str()));
    }
}

int SettingsWindow::SelectedRuleIndex() const {
    return ListView_GetNextItem(listRules_, -1, LVNI_SELECTED);
}

void SettingsWindow::BeginRecord(int slot) {
    recordingSlot_ = slot;
    if (host_.suspendHotkeys) {
        host_.suspendHotkeys();
    }
    SetWindowTextW(labelStatus_, L"录制中：请按下新的组合键（至少包含一个修饰键），Esc 取消。");
    RefreshHotkeyLabels();
    SetFocus(hwnd_);
}

void SettingsWindow::EndRecord(bool accept) {
    (void)accept;
    recordingSlot_ = 0;
    if (host_.resumeHotkeys) {
        host_.resumeHotkeys();
    }
    RefreshHotkeyLabels();
}

bool SettingsWindow::CaptureHotkey(WPARAM vk) {
    if (IsPureModifierKey(vk)) {
        return false;
    }
    if (vk == VK_ESCAPE) {
        SetWindowTextW(labelStatus_, L"已取消录制。");
        EndRecord(false);
        return true;
    }

    UINT mods = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
    if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
    if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000)) {
        mods |= MOD_WIN;
    }
    if (mods == 0) {
        SetWindowTextW(labelStatus_, L"需要至少一个修饰键（Ctrl / Alt / Shift / Win），请重按。");
        return false;
    }

    const core::HotkeySpec candidate = MakeSpec(mods, static_cast<UINT>(vk));
    bool conflict = false;
    const bool usable =
        host_.probeHotkey ? host_.probeHotkey(recordingSlot_, candidate, &conflict) : true;
    if (!usable) {
        SetWindowTextW(labelStatus_,
                       conflict ? L"该组合已被占用，请换一个。" : L"该组合无效，请换一个。");
        return false;  // stay in recording mode
    }

    if (core::HotkeySpec* target = SlotSpec(recordingSlot_)) {
        *target = candidate;
        dirty_ = true;
    }
    SetWindowTextW(labelStatus_, (L"已记录：" + FormatSpec(candidate) + L"（点「保存」生效）").c_str());
    EndRecord(true);
    return true;
}

void SettingsWindow::OnAddRule() {
    core::AppRule rule;
    rule.id = L"rule-" + std::to_wstring(rules_.size() + 1);
    rule.name = L"新规则";
    rule.actions.hideWindow = true;
    rule.actions.hideFromAltTab = true;
    if (RunRuleEditorDialog(instance_, hwnd_, host_.enumerateTargets, rule)) {
        rules_.push_back(rule);
        dirty_ = true;
        RefreshRules();
    }
}

void SettingsWindow::OnEditRule() {
    const int index = SelectedRuleIndex();
    if (index < 0 || static_cast<std::size_t>(index) >= rules_.size()) {
        SetWindowTextW(labelStatus_, L"请先在列表中选择一条规则。");
        return;
    }
    core::AppRule rule = rules_[static_cast<std::size_t>(index)];
    if (RunRuleEditorDialog(instance_, hwnd_, host_.enumerateTargets, rule)) {
        rules_[static_cast<std::size_t>(index)] = rule;
        dirty_ = true;
        RefreshRules();
    }
}

void SettingsWindow::OnDeleteRule() {
    const int index = SelectedRuleIndex();
    if (index < 0 || static_cast<std::size_t>(index) >= rules_.size()) {
        SetWindowTextW(labelStatus_, L"请先在列表中选择一条规则。");
        return;
    }
    rules_.erase(rules_.begin() + index);
    dirty_ = true;
    RefreshRules();
}

void SettingsWindow::OnSave() {
    if (store_ == nullptr) {
        return;
    }
    core::AppConfig& config = store_->MutableConfig();
    config.hotkey.hideToggle = working_[0];
    config.hotkey.openSettings = working_[1];
    config.rules = rules_;
    config.bringRestoredToFront = bringRestoredToFront_;
    if (!store_->Save(core::AppRuleStore::DefaultConfigPath())) {
        SetWindowTextW(labelStatus_, L"保存失败（无法写入配置文件）。");
        return;
    }
    if (host_.onConfigChanged) {
        host_.onConfigChanged();
    }
    dirty_ = false;
    SetWindowTextW(labelStatus_, L"已保存并立即生效。");
}

void SettingsWindow::OnQuitApp() {
    if (!host_.onQuitApp) {
        return;
    }
    const int answer = MessageBoxW(
        hwnd_,
        L"退出 HideUtil 吗？\n\n退出前会先恢复全部已隐藏的窗口、任务栏按钮、音频与已挂起的进程，"
        L"然后结束程序（两个全局热键随之失效）。",
        L"退出 HideUtil", MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (answer != IDOK) {
        return;
    }

    if (dirty_) {
        const int saveFirst = MessageBoxW(hwnd_, L"有尚未保存的设置改动，要先保存吗？",
                                          L"未保存的改动",
                                          MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON3);
        if (saveFirst == IDCANCEL) {
            return;
        }
        if (saveFirst == IDYES) {
            OnSave();
        }
    }

    host_.onQuitApp();
}

void SettingsWindow::Destroy() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsWindow* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self != nullptr) {
            self->hwnd_ = hwnd;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->HandleMessage(message, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int notify = HIWORD(wParam);
            switch (id) {
                case IDC_RECORD0: BeginRecord(kSlotHideToggle); return 0;
                case IDC_RECORD1: BeginRecord(kSlotOpenSettings); return 0;
                case IDC_BRING_TO_FRONT:
                    if (notify == BN_CLICKED && checkboxBringToFront_ != nullptr) {
                        bringRestoredToFront_ =
                            SendMessageW(checkboxBringToFront_, BM_GETCHECK, 0, 0) == BST_CHECKED;
                        dirty_ = true;
                        SetWindowTextW(labelStatus_, L"恢复时置顶已修改（点「保存」生效）。");
                    }
                    return 0;
                case IDC_ADD: OnAddRule(); return 0;
                case IDC_EDIT: OnEditRule(); return 0;
                case IDC_DELETE: OnDeleteRule(); return 0;
                case IDC_SAVE: OnSave(); return 0;
                case IDC_QUIT: OnQuitApp(); return 0;
                case IDC_CANCEL:
                    if (dirty_) {
                        working_[0] = store_->Config().hotkey.hideToggle;
                        working_[1] = store_->Config().hotkey.openSettings;
                        rules_ = store_->Config().rules;
                        bringRestoredToFront_ = store_->Config().bringRestoredToFront;
                        if (checkboxBringToFront_ != nullptr) {
                            SendMessageW(checkboxBringToFront_, BM_SETCHECK,
                                         bringRestoredToFront_ ? BST_CHECKED : BST_UNCHECKED, 0);
                        }
                        dirty_ = false;
                    }
                    Destroy();
                    return 0;
                default: break;
            }
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (recordingSlot_ != 0 && CaptureHotkey(wParam)) {
                return 0;
            }
            break;
        case WM_CLOSE:
            if (recordingSlot_ != 0) {
                EndRecord(false);
            }
            Destroy();
            return 0;
        case WM_DESTROY:
            hwnd_ = nullptr;
            s_instance_ = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

}  // namespace ui
}  // namespace hideutil
