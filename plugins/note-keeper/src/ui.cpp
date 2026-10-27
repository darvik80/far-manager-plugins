#include "ui.hpp"
#include "guid.hpp"
#include "nk_text.hpp"
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

HANDLE OpenGuardedDialog(FarDialogItem* items, size_t count, int width, int height, intptr_t cancelId,
                         const GUID& dialogId = DialogGuid) {
    HANDLE dialog = FarOpenDialog(g_info, MainGuid, dialogId, items, count, width, height);
    if ((!dialog || dialog == INVALID_HANDLE_VALUE) && g_info.DefDlgProc)
        dialog = FarOpenDialog(g_info, MainGuid, dialogId, items, count, width, height, GuardDlgProc, cancelId);
    return dialog;
}

}  // namespace

void EatLeftoverHotkeys() {
    DrainLeftoverHotkeys();
}

void Say(const wchar_t* text) {
    const wchar_t* items[] = { L"NoteKeeper", L"", text };
    if (g_info.Message)
        g_info.Message(MainGuid, MenuGuid, FMSG_WARNING | FMSG_MB_OK, nullptr, items, 3, 1);
}

bool AskYes(const wchar_t* text) {
    const wchar_t* items[] = { L"NoteKeeper", L"", text };
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
        const wchar_t* value = reinterpret_cast<const wchar_t*>(g_info.SendDlgMessage(dialog, 63, 2, nullptr));
        if (value) text = value;
    }
    g_info.DialogFree(dialog);
    return text;
}

bool EditTodoMeta(NkTodo& todo) {
    wchar_t title[256]{};
    wchar_t reminder[32]{};
    wcsncpy_s(title, todo.title.c_str(), _TRUNCATE);
    const std::wstring localReminder = todo.reminder.empty() ? std::wstring{} : NkIsoToLocal(todo.reminder);
    wcsncpy_s(reminder, localReminder.c_str(), _TRUNCATE);
    const std::wstring repeat = NkLower(todo.repeat.empty() ? L"none" : todo.repeat);
    const bool telegram = todo.notificationChannels.empty() ||
                          NkLower(todo.notificationChannels).find(L"telegram") != std::wstring::npos;
    const bool dingtalk = NkLower(todo.notificationChannels).find(L"dingtalk") != std::wstring::npos;

    FarDialogItem items[16]{};
    items[0].Type = DI_DOUBLEBOX;
    items[0].X1 = 0;
    items[0].Y1 = 0;
    items[0].X2 = 58;
    items[0].Y2 = 14;
    items[0].Data = todo.id.empty() ? L" Новый todo " : L" Todo ";

    items[1].Type = DI_TEXT;
    items[1].X1 = 2;
    items[1].Y1 = 2;
    items[1].Data = L"Имя:";
    items[2].Type = DI_EDIT;
    items[2].X1 = 14;
    items[2].Y1 = 2;
    items[2].X2 = 56;
    items[2].Y2 = 2;
    items[2].Data = title;
    items[2].MaxLength = 255;
    items[2].Flags = DIF_FOCUS;

    items[3].Type = DI_TEXT;
    items[3].X1 = 2;
    items[3].Y1 = 4;
    items[3].Data = L"Повтор:";
    items[4].Type = DI_RADIOBUTTON;
    items[4].X1 = 14;
    items[4].Y1 = 4;
    items[4].Flags = DIF_GROUP;
    items[4].Param.Selected = (repeat == L"none" || repeat.empty()) ? 1 : 0;
    items[4].Data = L"Нет";
    items[5].Type = DI_RADIOBUTTON;
    items[5].X1 = 24;
    items[5].Y1 = 4;
    items[5].Param.Selected = repeat == L"daily" ? 1 : 0;
    items[5].Data = L"День";
    items[6].Type = DI_RADIOBUTTON;
    items[6].X1 = 36;
    items[6].Y1 = 4;
    items[6].Param.Selected = repeat == L"weekly" ? 1 : 0;
    items[6].Data = L"Нед.";
    items[7].Type = DI_RADIOBUTTON;
    items[7].X1 = 48;
    items[7].Y1 = 4;
    items[7].Param.Selected = repeat == L"monthly" ? 1 : 0;
    items[7].Data = L"Мес.";

    items[8].Type = DI_TEXT;
    items[8].X1 = 2;
    items[8].Y1 = 6;
    items[8].Data = L"Напомнить:";
    items[9].Type = DI_EDIT;
    items[9].X1 = 14;
    items[9].Y1 = 6;
    items[9].X2 = 56;
    items[9].Y2 = 6;
    items[9].Data = reminder;
    items[9].MaxLength = 31;

    items[10].Type = DI_TEXT;
    items[10].X1 = 2;
    items[10].Y1 = 8;
    items[10].Data = L"Каналы:";
    items[11].Type = DI_CHECKBOX;
    items[11].X1 = 14;
    items[11].Y1 = 8;
    items[11].Param.Selected = telegram ? 1 : 0;
    items[11].Data = L"Telegram";
    items[12].Type = DI_CHECKBOX;
    items[12].X1 = 32;
    items[12].Y1 = 8;
    items[12].Param.Selected = dingtalk ? 1 : 0;
    items[12].Data = L"DingTalk";

    items[13].Type = DI_TEXT;
    items[13].Y1 = 10;
    items[13].Flags = DIF_SEPARATOR;

    items[14].Type = DI_BUTTON;
    items[14].X1 = 14;
    items[14].Y1 = 12;
    items[14].Flags = DIF_DEFAULTBUTTON | DIF_CENTERGROUP;
    items[14].Data = L"Текст";
    items[15].Type = DI_BUTTON;
    items[15].X1 = 30;
    items[15].Y1 = 12;
    items[15].Flags = DIF_CENTERGROUP;
    items[15].Data = L"Отмена";

    HANDLE dialog = OpenGuardedDialog(items, 16, 59, 15, 15, TodoDialogGuid);
    if (!dialog || dialog == INVALID_HANDLE_VALUE) {
        Say(L"Не удалось открыть диалог todo.");
        return false;
    }
    const intptr_t result = g_info.DialogRun(dialog);
    const bool saved = result == 14;
    if (saved) {
        const wchar_t* t = reinterpret_cast<const wchar_t*>(g_info.SendDlgMessage(dialog, 63, 2, nullptr));
        const wchar_t* r = reinterpret_cast<const wchar_t*>(g_info.SendDlgMessage(dialog, 63, 9, nullptr));
        if (t) wcsncpy_s(title, t, _TRUNCATE);
        if (r) wcsncpy_s(reminder, r, _TRUNCATE);
        if (g_info.SendDlgMessage(dialog, 25, 7, nullptr) == 1) todo.repeat = L"monthly";
        else if (g_info.SendDlgMessage(dialog, 25, 6, nullptr) == 1) todo.repeat = L"weekly";
        else if (g_info.SendDlgMessage(dialog, 25, 5, nullptr) == 1) todo.repeat = L"daily";
        else todo.repeat = L"none";
        std::wstring channels;
        if (g_info.SendDlgMessage(dialog, 25, 11, nullptr) == 1) channels = L"telegram";
        if (g_info.SendDlgMessage(dialog, 25, 12, nullptr) == 1) {
            if (!channels.empty()) channels += L",";
            channels += L"dingtalk";
        }
        todo.notificationChannels = channels;
    }
    g_info.DialogFree(dialog);
    if (!saved) return false;
    if (title[0] == L'\0') {
        Say(L"Нужно имя todo.");
        return false;
    }
    todo.title = title;
    todo.reminder = reminder[0] ? NkLocalToIso(reminder) : std::wstring{};
    return true;
}

bool EditSyncSettings(std::wstring& baseUrl, std::wstring& token) {
    wchar_t url[512]{};
    wchar_t pat[512]{};
    if (baseUrl.empty()) wcsncpy_s(url, L"https://", _TRUNCATE);
    else wcsncpy_s(url, baseUrl.c_str(), _TRUNCATE);
    wcsncpy_s(pat, token.c_str(), _TRUNCATE);
    FarDialogItem items[8]{};
    items[0].Type = DI_DOUBLEBOX;
    items[0].X1 = 0;
    items[0].Y1 = 0;
    items[0].X2 = 62;
    items[0].Y2 = 9;
    items[0].Data = L" NoteKeeper · HTTPS ";
    items[1].Type = DI_TEXT;
    items[1].X1 = 2;
    items[1].Y1 = 2;
    items[1].Y2 = 2;
    items[1].Data = L"URL:";
    items[2].Type = DI_EDIT;
    items[2].X1 = 10;
    items[2].Y1 = 2;
    items[2].X2 = 60;
    items[2].Y2 = 2;
    items[2].Data = url;
    items[2].MaxLength = 511;
    items[2].Flags = DIF_FOCUS;
    items[3].Type = DI_TEXT;
    items[3].X1 = 2;
    items[3].Y1 = 4;
    items[3].Y2 = 4;
    items[3].Data = L"PAT:";
    items[4].Type = DI_PSWEDIT;
    items[4].X1 = 10;
    items[4].Y1 = 4;
    items[4].X2 = 60;
    items[4].Y2 = 4;
    items[4].Data = pat;
    items[4].MaxLength = 511;
    items[5].Type = DI_TEXT;
    items[5].Y1 = 6;
    items[5].Flags = DIF_SEPARATOR;
    items[6].Type = DI_BUTTON;
    items[6].X1 = 18;
    items[6].Y1 = 8;
    items[6].Flags = DIF_DEFAULTBUTTON | DIF_CENTERGROUP;
    items[6].Data = L"OK";
    items[7].Type = DI_BUTTON;
    items[7].X1 = 32;
    items[7].Y1 = 8;
    items[7].Flags = DIF_CENTERGROUP;
    items[7].Data = L"Отмена";
    HANDLE dialog = OpenGuardedDialog(items, 8, 63, 10, 7);
    if (!dialog || dialog == INVALID_HANDLE_VALUE) {
        if (!g_info.InputBox) {
            Say(L"Не удалось открыть диалог настроек.");
            return false;
        }
        wchar_t urlOut[512]{};
        wchar_t patOut[512]{};
        wcsncpy_s(urlOut, url, _TRUNCATE);
        wcsncpy_s(patOut, pat, _TRUNCATE);
        if (!g_info.InputBox(MainGuid, DialogGuid, L"NoteKeeper", L"URL сервера", L"NkUrl",
                             urlOut, urlOut, 512, nullptr, 0x1))
            return false;
        if (!g_info.InputBox(MainGuid, DialogGuid, L"NoteKeeper", L"PAT (notes,todos или mcp)", L"NkPat",
                             patOut, patOut, 512, nullptr, 0x1 | 0x2))
            return false;
        baseUrl = urlOut;
        token = patOut;
        return true;
    }
    const intptr_t result = g_info.DialogRun(dialog);
    const bool saved = result == 6;
    if (saved) {
        const wchar_t* u = reinterpret_cast<const wchar_t*>(g_info.SendDlgMessage(dialog, 63, 2, nullptr));
        const wchar_t* t = reinterpret_cast<const wchar_t*>(g_info.SendDlgMessage(dialog, 63, 4, nullptr));
        if (u) wcsncpy_s(url, u, _TRUNCATE);
        if (t) wcsncpy_s(pat, t, _TRUNCATE);
    }
    g_info.DialogFree(dialog);
    if (!saved) return false;
    baseUrl = NkNormalizeUrl(url);
    token = pat;
    if (baseUrl.empty() && token.empty()) return true;
    NkSettings nk;
    nk.baseUrl = baseUrl;
    nk.token = token;
    std::wstring notesError;
    std::wstring todosError;
    if (!NkTestConnection(nk, L"/notes", notesError) && !NkTestConnection(nk, L"/todos", todosError)) {
        Say(notesError.c_str());
        return false;
    }
    return true;
}
