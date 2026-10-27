#include "ui.hpp"
#include "guid.hpp"
#include "far/dialog.hpp"

PluginStartupInfo g_info{};

namespace {

bool g_eatHotkey = false;
bool g_seenHotkey = false;
intptr_t g_cancelId = -1;

bool IsKeyEvent(WORD eventType) {
    return (eventType & 0x7FFF) == KEY_EVENT;
}

bool IsLeftoverHotkey(WORD vk) {
    return vk == VK_RETURN || vk == VK_F4 || vk == VK_F7;
}

bool IsLeftoverKey(const void* param2) {
    if (!param2) return false;
    const auto* rec = static_cast<const INPUT_RECORD*>(param2);
    if (!IsKeyEvent(rec->EventType) || !rec->Event.KeyEvent.bKeyDown) return false;
    return IsLeftoverHotkey(rec->Event.KeyEvent.wVirtualKeyCode);
}

bool HotkeysAreUp() {
    return !(GetAsyncKeyState(VK_F4) & 0x8000) &&
           !(GetAsyncKeyState(VK_F7) & 0x8000) &&
           !(GetAsyncKeyState(VK_RETURN) & 0x8000);
}

void DrainLeftoverHotkeys() {
    for (int i = 0; i < 40 && !HotkeysAreUp(); ++i)
        Sleep(15);
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (!input || input == INVALID_HANDLE_VALUE) return;
    for (int i = 0; i < 64; ++i) {
        INPUT_RECORD rec{};
        DWORD count = 0;
        if (!PeekConsoleInputW(input, &rec, 1, &count) || count == 0) break;
        if (!IsKeyEvent(rec.EventType) || !IsLeftoverHotkey(rec.Event.KeyEvent.wVirtualKeyCode))
            break;
        DWORD read = 0;
        if (!ReadConsoleInputW(input, &rec, 1, &read) || read == 0) break;
    }
}

intptr_t WINAPI GuardDlgProc(HANDLE dlg, intptr_t msg, intptr_t param1, void* param2) {
    if (msg == DN_INITDIALOG) {
        g_eatHotkey = true;
        g_seenHotkey = false;
        g_cancelId = reinterpret_cast<intptr_t>(param2);
        if (g_info.SendDlgMessage)
            g_info.SendDlgMessage(dlg, DM_SETINPUTNOTIFY, 1, nullptr);
    }
    if (msg == DN_BTNCLICK)
        g_eatHotkey = false;
    if (g_eatHotkey && (msg == DN_INPUT || msg == DN_CONTROLINPUT) && IsLeftoverKey(param2)) {
        g_seenHotkey = true;
        return msg == DN_INPUT ? 0 : 1;
    }
    if (g_eatHotkey && g_seenHotkey && HotkeysAreUp())
        g_eatHotkey = false;
    if (msg == DN_CLOSE && g_eatHotkey && param1 >= 0 && param1 != g_cancelId)
        return 0;
    if (g_info.DefDlgProc)
        return g_info.DefDlgProc(dlg, msg, param1, param2);
    return 1;
}

}  // namespace

HANDLE OpenGuardedDialog(FarDialogItem* items, size_t count, int width, int height, intptr_t cancelId) {
    HANDLE dialog = FarOpenDialog(g_info, MainGuid, DialogGuid, items, count, width, height);
    if ((!dialog || dialog == INVALID_HANDLE_VALUE) && g_info.DefDlgProc)
        dialog = FarOpenDialog(g_info, MainGuid, DialogGuid, items, count, width, height, GuardDlgProc, cancelId);
    return dialog;
}

void Say(const wchar_t* text) {
    const wchar_t* items[] = { L"SSH", L"", text };
    if (g_info.Message)
        g_info.Message(MainGuid, MenuGuid, FMSG_WARNING | FMSG_MB_OK, nullptr, items, 3, 1);
}

bool AskYes(const wchar_t* text) {
    const wchar_t* items[] = { L"SSH", L"", text };
    if (!g_info.Message) return false;
    return g_info.Message(MainGuid, MenuGuid, FMSG_WARNING | FMSG_MB_YESNO, nullptr, items, 3, 2) == 0;
}

std::wstring AskText(const wchar_t* title, const wchar_t* prompt, bool password) {
    if (!g_info.DialogInit || !g_info.DialogRun || !g_info.DialogFree) return {};

    FarDialogItem items[4]{};
    items[0].Type = DI_DOUBLEBOX;
    items[0].X1 = 0;
    items[0].Y1 = 0;
    items[0].X2 = 55;
    items[0].Y2 = 6;
    items[0].Data = title;

    items[1].Type = DI_TEXT;
    items[1].X1 = 2;
    items[1].Y1 = 2;
    items[1].X2 = 52;
    items[1].Y2 = 2;
    items[1].Data = prompt;

    items[2].Type = password ? DI_PSWEDIT : DI_EDIT;
    items[2].X1 = 2;
    items[2].Y1 = 3;
    items[2].X2 = 52;
    items[2].Y2 = 3;
    items[2].Data = L"";

    items[3].Type = DI_BUTTON;
    items[3].X1 = 22;
    items[3].Y1 = 5;
    items[3].X2 = 32;
    items[3].Y2 = 5;
    items[3].Flags = DIF_DEFAULTBUTTON | DIF_CENTERGROUP;
    items[3].Data = L"OK";

    HANDLE dialog = OpenGuardedDialog(items, 4, 56, 7, -1);
    if (!dialog || dialog == INVALID_HANDLE_VALUE) return {};
    const intptr_t result = g_info.DialogRun(dialog);
    std::wstring text;
    if (result == 3 && g_info.SendDlgMessage) {
        const wchar_t* value = reinterpret_cast<const wchar_t*>(
            g_info.SendDlgMessage(dialog, 63, 2, nullptr));
        if (value) text = value;
    }
    g_info.DialogFree(dialog);
    return text;
}
