#pragma once

// Shared layout for plugin dialogs. Use these numbers so SSH, Vault and
// NoteKeeper forms look like one family: label column, edit column, footer
// with { OK } [ Отмена ], unique DialogGuid, leftover-Enter guard.

#include "dialog.hpp"

#include <string>

namespace farform {

constexpr intptr_t kWidth = 63;
constexpr intptr_t kBoxX2 = 62;
constexpr intptr_t kLabelX = 2;
constexpr intptr_t kEditX = 12;
constexpr intptr_t kInnerR = 60;
constexpr intptr_t kRow0 = 2;
constexpr intptr_t kRowStep = 2;
constexpr intptr_t kAskW = 56;
constexpr intptr_t kAskH = 7;

inline std::wstring PaddedTitle(const wchar_t* name) {
    std::wstring text = L" ";
    if (name && *name) {
        if (name[0] == L' ') return name;
        text += name;
    }
    text += L' ';
    return text;
}

inline void FillBox(FarDialogItem& item, intptr_t width, intptr_t height, const wchar_t* title) {
    item = {};
    item.Type = DI_DOUBLEBOX;
    item.X1 = 0;
    item.Y1 = 0;
    item.X2 = width - 1;
    item.Y2 = height - 1;
    item.Data = title;
}

inline void FillLabel(FarDialogItem& item, intptr_t y, const wchar_t* text, intptr_t x = kLabelX) {
    item = {};
    item.Type = DI_TEXT;
    item.X1 = x;
    item.Y1 = y;
    item.Y2 = y;
    item.Data = text;
}

inline void FillEdit(FarDialogItem& item, intptr_t y, intptr_t x1, intptr_t x2, const wchar_t* value,
                     intptr_t maxLen, bool password, uint64_t extraFlags = 0) {
    item = {};
    item.Type = password ? DI_PSWEDIT : DI_EDIT;
    item.X1 = x1;
    item.Y1 = y;
    item.X2 = x2;
    item.Y2 = y;
    item.Data = value ? value : L"";
    item.MaxLength = maxLen;
    item.Flags = extraFlags;
}

inline void FillSeparator(FarDialogItem& item, intptr_t y) {
    item = {};
    item.Type = DI_TEXT;
    item.Y1 = y;
    item.Flags = DIF_SEPARATOR;
}

inline void FillButton(FarDialogItem& item, intptr_t y, const wchar_t* text, bool def) {
    item = {};
    item.Type = DI_BUTTON;
    item.Y1 = y;
    item.Y2 = y;
    item.Data = text;
    item.Flags = DIF_CENTERGROUP | (def ? DIF_DEFAULTBUTTON : 0);
}

namespace detail {

inline thread_local const PluginStartupInfo* g_info = nullptr;
inline thread_local bool g_eatReturn = false;
inline thread_local bool g_eatF4 = false;
inline thread_local bool g_eatF7 = false;
inline thread_local intptr_t g_cancel = -1;

inline bool Eating() {
    return g_eatReturn || g_eatF4 || g_eatF7;
}

inline bool Down(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

inline void ReleaseIfUp() {
    if (g_eatReturn && !Down(VK_RETURN)) g_eatReturn = false;
    if (g_eatF4 && !Down(VK_F4)) g_eatF4 = false;
    if (g_eatF7 && !Down(VK_F7)) g_eatF7 = false;
}

inline bool EatVk(WORD vk) {
    if (vk == VK_RETURN) return g_eatReturn;
    if (vk == VK_F4) return g_eatF4;
    if (vk == VK_F7) return g_eatF7;
    return false;
}

inline bool LeftoverKey(const void* param2) {
    if (!param2) return false;
    const auto* rec = static_cast<const INPUT_RECORD*>(param2);
    if (((rec->EventType & 0x7FFF) != KEY_EVENT) || !rec->Event.KeyEvent.bKeyDown) return false;
    return EatVk(rec->Event.KeyEvent.wVirtualKeyCode);
}

inline intptr_t WINAPI GuardProc(HANDLE dlg, intptr_t msg, intptr_t param1, void* param2) {
    const PluginStartupInfo* info = g_info;
    if (msg == DN_INITDIALOG) {
        g_eatReturn = Down(VK_RETURN);
        g_eatF4 = Down(VK_F4);
        g_eatF7 = Down(VK_F7);
        g_cancel = reinterpret_cast<intptr_t>(param2);
        if (info && info->SendDlgMessage)
            info->SendDlgMessage(dlg, DM_SETINPUTNOTIFY, 1, nullptr);
    }
    ReleaseIfUp();
    if (msg == DN_DRAWDIALOG)
        return info && info->DefDlgProc ? info->DefDlgProc(dlg, msg, param1, param2) : 1;
    if (msg == DN_BTNCLICK) {
        g_eatReturn = g_eatF4 = g_eatF7 = false;
    }
    if (Eating() && (msg == DN_INPUT || msg == DN_CONTROLINPUT) && LeftoverKey(param2))
        return msg == DN_INPUT ? 0 : 1;
    if (msg == DN_CLOSE && Eating() && param1 >= 0 && param1 != g_cancel)
        return 0;
    if (info && info->DefDlgProc)
        return info->DefDlgProc(dlg, msg, param1, param2);
    return 1;
}

}  // namespace detail

inline HANDLE OpenGuarded(
    const PluginStartupInfo& info,
    const GUID& pluginId,
    const GUID& dialogId,
    FarDialogItem* items,
    size_t count,
    intptr_t width,
    intptr_t height,
    intptr_t cancelId)
{
    detail::g_info = &info;
    HANDLE dialog = FarOpenDialog(info, pluginId, dialogId, items, count, width, height, detail::GuardProc, cancelId);
    if (!dialog || dialog == INVALID_HANDLE_VALUE)
        dialog = FarOpenDialog(info, pluginId, dialogId, items, count, width, height);
    return dialog;
}

inline std::wstring AskText(
    const PluginStartupInfo& info,
    const GUID& pluginId,
    const GUID& dialogId,
    const wchar_t* title,
    const wchar_t* prompt,
    bool password)
{
    if (!info.DialogInit || !info.DialogRun || !info.DialogFree) return {};
    const std::wstring boxed = PaddedTitle(title);
    FarDialogItem items[4]{};
    FillBox(items[0], kAskW, kAskH, boxed.c_str());
    FillLabel(items[1], 2, prompt);
    items[1].X2 = kAskW - 4;
    FillEdit(items[2], 3, kLabelX, kAskW - 4, L"", 0, password, DIF_FOCUS);
    FillButton(items[3], 5, L"OK", true);
    HANDLE dialog = OpenGuarded(info, pluginId, dialogId, items, 4, kAskW, kAskH, -1);
    if (!dialog || dialog == INVALID_HANDLE_VALUE) return {};
    const intptr_t result = info.DialogRun(dialog);
    std::wstring text;
    if (result == 3 && info.SendDlgMessage) {
        const wchar_t* value = reinterpret_cast<const wchar_t*>(info.SendDlgMessage(dialog, 63, 2, nullptr));
        if (value) text = value;
    }
    info.DialogFree(dialog);
    return text;
}

}  // namespace farform
