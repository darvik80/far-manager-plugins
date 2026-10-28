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

inline void ReadEdit(const PluginStartupInfo& info, HANDLE dlg, intptr_t id, wchar_t* buf, size_t size) {
    if (!buf || size == 0) return;
    buf[0] = 0;
    if (!info.SendDlgMessage) return;
    FarDialogItemData data{};
    data.StructSize = sizeof(data);
    data.PtrLength = static_cast<intptr_t>(size - 1);
    data.PtrData = buf;
    info.SendDlgMessage(dlg, DM_GETTEXT, id, &data);
    buf[size - 1] = 0;
}

namespace detail {

inline thread_local const PluginStartupInfo* g_info = nullptr;
inline thread_local DWORD g_eatUntil = 0;
inline thread_local intptr_t g_cancel = -1;

inline bool GuardActive() {
    return g_eatUntil != 0 && GetTickCount() < g_eatUntil;
}

inline bool LeftoverKey(const void* param2) {
    if (!param2) return false;
    const auto* rec = static_cast<const INPUT_RECORD*>(param2);
    if (((rec->EventType & 0x7FFF) != KEY_EVENT) || !rec->Event.KeyEvent.bKeyDown) return false;
    const WORD vk = rec->Event.KeyEvent.wVirtualKeyCode;
    return vk == VK_RETURN || vk == VK_F4 || vk == VK_F7;
}

inline intptr_t WINAPI GuardProc(HANDLE dlg, intptr_t msg, intptr_t param1, void* param2) {
    const PluginStartupInfo* info = g_info;
    if (msg == DN_INITDIALOG) {
        g_cancel = reinterpret_cast<intptr_t>(param2);
        g_eatUntil = GetTickCount() + 400;
        if (info && info->SendDlgMessage)
            info->SendDlgMessage(dlg, DM_SETINPUTNOTIFY, 1, nullptr);
    }
    if (msg == DN_EDITCHANGE || msg == DN_BTNCLICK)
        g_eatUntil = 0;
    if (msg == DN_DRAWDIALOG)
        return info && info->DefDlgProc ? info->DefDlgProc(dlg, msg, param1, param2) : 1;
    if (GuardActive() && (msg == DN_INPUT || msg == DN_CONTROLINPUT) && LeftoverKey(param2))
        return msg == DN_INPUT ? 0 : 1;
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
    intptr_t cancelId,
    intptr_t (WINAPI *proc)(HANDLE, intptr_t, intptr_t, void*) = nullptr)
{
    detail::g_info = &info;
    if (!proc) proc = detail::GuardProc;
    HANDLE dialog = FarOpenDialog(info, pluginId, dialogId, items, count, width, height, proc, cancelId);
    if (!dialog || dialog == INVALID_HANDLE_VALUE)
        dialog = FarOpenDialog(info, pluginId, dialogId, items, count, width, height, proc, cancelId);
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
    if (result == 3) {
        wchar_t buf[512]{};
        ReadEdit(info, dialog, 2, buf, 512);
        text = buf;
    }
    info.DialogFree(dialog);
    return text;
}

}  // namespace farform
