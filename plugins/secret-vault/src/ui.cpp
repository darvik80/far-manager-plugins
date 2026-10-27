#include "ui.hpp"
#include "guid.hpp"
#include "far/dialog.hpp"
#include "nk_api.hpp"

#include <vector>

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

HANDLE OpenGuardedDialog(FarDialogItem* items, size_t count, intptr_t width, intptr_t height,
                         intptr_t cancelId, const GUID& dialogId = DialogGuid) {
    HANDLE dialog = FarOpenDialog(g_info, MainGuid, dialogId, items, count, width, height);
    if ((!dialog || dialog == INVALID_HANDLE_VALUE) && g_info.DefDlgProc)
        dialog = FarOpenDialog(g_info, MainGuid, dialogId, items, count, width, height, GuardDlgProc, cancelId);
    return dialog;
}

}  // namespace

void Say(const wchar_t* text) {
    const wchar_t* items[] = { L"Secret Vault", L"", text };
    if (g_info.Message)
        g_info.Message(MainGuid, MenuGuid, FMSG_WARNING | FMSG_MB_OK, nullptr, items, 3, 1);
}

bool AskYes(const wchar_t* text) {
    const wchar_t* items[] = { L"Secret Vault", L"", text };
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

intptr_t Pick(const wchar_t* title, const std::vector<std::wstring>& lines) {
    if (!g_info.Menu || lines.empty()) return -1;
    std::vector<FarMenuItem> items(lines.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        items[i].Text = lines[i].c_str();
        if (i == 0) items[i].Flags = 0x10000;
    }
    return g_info.Menu(MainGuid, MenuGuid, -1, -1, 0, 0x2, title, L"Enter — выбрать, Esc — назад", nullptr, nullptr, nullptr, items.data(), items.size());
}

bool CreateKey(KeyRecord& item, std::wstring& kind) {
    wchar_t name[128]{};
    wchar_t comment[256]{};
    wcsncpy_s(name, item.name.c_str(), _TRUNCATE);
    wcsncpy_s(comment, item.comment.c_str(), _TRUNCATE);

    FarDialogItem items[12]{};
    items[0].Type = DI_DOUBLEBOX;
    items[0].X1 = 0;
    items[0].Y1 = 0;
    items[0].X2 = 58;
    items[0].Y2 = 14;
    items[0].Data = L" Новый ключ ";

    items[1].Type = DI_TEXT;
    items[1].X1 = 2;
    items[1].Y1 = 2;
    items[1].Data = L"Имя:";
    items[2].Type = DI_EDIT;
    items[2].X1 = 14;
    items[2].Y1 = 2;
    items[2].X2 = 56;
    items[2].Y2 = 2;
    items[2].Data = name;
    items[2].MaxLength = 127;
    items[2].Flags = DIF_FOCUS;

    items[3].Type = DI_TEXT;
    items[3].X1 = 2;
    items[3].Y1 = 4;
    items[3].Data = L"Коммент:";
    items[4].Type = DI_EDIT;
    items[4].X1 = 14;
    items[4].Y1 = 4;
    items[4].X2 = 56;
    items[4].Y2 = 4;
    items[4].Data = comment;
    items[4].MaxLength = 255;

    items[5].Type = DI_TEXT;
    items[5].X1 = 2;
    items[5].Y1 = 6;
    items[5].Data = L"Тип:";

    items[6].Type = DI_RADIOBUTTON;
    items[6].X1 = 14;
    items[6].Y1 = 6;
    items[6].X2 = 56;
    items[6].Flags = DIF_GROUP;
    items[6].Param.Selected = 1;
    items[6].Data = L"создать ed25519";

    items[7].Type = DI_RADIOBUTTON;
    items[7].X1 = 14;
    items[7].Y1 = 7;
    items[7].X2 = 56;
    items[7].Data = L"создать RSA 3072";

    items[8].Type = DI_RADIOBUTTON;
    items[8].X1 = 14;
    items[8].Y1 = 8;
    items[8].X2 = 56;
    items[8].Data = L"импорт OpenSSH-файла";

    items[9].Type = DI_TEXT;
    items[9].Y1 = 10;
    items[9].Flags = DIF_SEPARATOR;

    items[10].Type = DI_BUTTON;
    items[10].X1 = 16;
    items[10].Y1 = 12;
    items[10].Flags = DIF_DEFAULTBUTTON | DIF_CENTERGROUP;
    items[10].Data = L"Создать";
    items[11].Type = DI_BUTTON;
    items[11].X1 = 30;
    items[11].Y1 = 12;
    items[11].Flags = DIF_CENTERGROUP;
    items[11].Data = L"Отмена";

    HANDLE dialog = OpenGuardedDialog(items, 12, 59, 15, 11, KeyCreateDialogGuid);
    if (!dialog || dialog == INVALID_HANDLE_VALUE) {
        Say(L"Не удалось открыть диалог ключа.");
        return false;
    }
    const intptr_t result = g_info.DialogRun(dialog);
    const bool saved = result == 10;
    if (saved) {
        const wchar_t* n = reinterpret_cast<const wchar_t*>(g_info.SendDlgMessage(dialog, 63, 2, nullptr));
        const wchar_t* c = reinterpret_cast<const wchar_t*>(g_info.SendDlgMessage(dialog, 63, 4, nullptr));
        if (n) wcsncpy_s(name, n, _TRUNCATE);
        if (c) wcsncpy_s(comment, c, _TRUNCATE);
        if (g_info.SendDlgMessage(dialog, 25, 8, nullptr) == 1) kind = L"import";
        else if (g_info.SendDlgMessage(dialog, 25, 7, nullptr) == 1) kind = L"rsa";
        else kind = L"ed25519";
    }
    g_info.DialogFree(dialog);
    if (!saved) return false;
    if (name[0] == L'\0') {
        Say(L"Нужно имя записи.");
        return false;
    }
    item.name = name;
    item.comment = comment;
    return true;
}

std::wstring AskFile(const wchar_t* title, bool save) {
    wchar_t path[MAX_PATH]{};
    if (g_info.InputBox)
        g_info.InputBox(MainGuid, MenuGuid, title, save ? L"Куда сохранить" : L"Откуда взять", L"SecretVaultFile", L"", path, MAX_PATH, nullptr, 0x1);
    return path;
}

bool EditPassword(PasswordRecord& item, bool create) {
    wchar_t name[128]{};
    wchar_t user[128]{};
    wchar_t secret[256]{};
    wchar_t host[256]{};
    wchar_t note[256]{};
    wcsncpy_s(name, item.name.c_str(), _TRUNCATE);
    wcsncpy_s(user, item.user.c_str(), _TRUNCATE);
    wcsncpy_s(secret, item.secret.c_str(), _TRUNCATE);
    wcsncpy_s(host, item.host.c_str(), _TRUNCATE);
    wcsncpy_s(note, item.note.c_str(), _TRUNCATE);

    FarDialogItem items[14]{};
    items[0].Type = DI_DOUBLEBOX;
    items[0].X1 = 0;
    items[0].Y1 = 0;
    items[0].X2 = 58;
    items[0].Y2 = 14;
    items[0].Data = create ? L" Новый пароль " : L" Пароль ";

    items[1].Type = DI_TEXT;
    items[1].X1 = 2;
    items[1].Y1 = 2;
    items[1].Y2 = 2;
    items[1].Data = L"Имя:";
    items[2].Type = DI_EDIT;
    items[2].X1 = 14;
    items[2].Y1 = 2;
    items[2].X2 = 56;
    items[2].Y2 = 2;
    items[2].Data = name;
    items[2].MaxLength = 127;
    items[2].Flags = DIF_FOCUS;

    items[3].Type = DI_TEXT;
    items[3].X1 = 2;
    items[3].Y1 = 4;
    items[3].Y2 = 4;
    items[3].Data = L"Логин:";
    items[4].Type = DI_EDIT;
    items[4].X1 = 14;
    items[4].Y1 = 4;
    items[4].X2 = 32;
    items[4].Y2 = 4;
    items[4].Data = user;
    items[4].MaxLength = 127;

    items[7].Type = DI_TEXT;
    items[7].X1 = 35;
    items[7].Y1 = 4;
    items[7].Y2 = 4;
    items[7].Data = L"Хост:";
    items[8].Type = DI_EDIT;
    items[8].X1 = 42;
    items[8].Y1 = 4;
    items[8].X2 = 56;
    items[8].Y2 = 4;
    items[8].Data = host;
    items[8].MaxLength = 255;

    items[5].Type = DI_TEXT;
    items[5].X1 = 2;
    items[5].Y1 = 6;
    items[5].Y2 = 6;
    items[5].Data = L"Пароль:";
    items[6].Type = DI_PSWEDIT;
    items[6].X1 = 14;
    items[6].Y1 = 6;
    items[6].X2 = 56;
    items[6].Y2 = 6;
    items[6].Data = secret;
    items[6].MaxLength = 255;

    items[9].Type = DI_TEXT;
    items[9].X1 = 2;
    items[9].Y1 = 8;
    items[9].Y2 = 8;
    items[9].Data = L"Заметка:";
    items[10].Type = DI_EDIT;
    items[10].X1 = 14;
    items[10].Y1 = 8;
    items[10].X2 = 56;
    items[10].Y2 = 8;
    items[10].Data = note;
    items[10].MaxLength = 255;

    items[11].Type = DI_TEXT;
    items[11].Y1 = 10;
    items[11].Flags = DIF_SEPARATOR;

    items[12].Type = DI_BUTTON;
    items[12].X1 = 16;
    items[12].Y1 = 12;
    items[12].Flags = DIF_DEFAULTBUTTON | DIF_CENTERGROUP;
    items[12].Data = L"Сохранить";
    items[13].Type = DI_BUTTON;
    items[13].X1 = 30;
    items[13].Y1 = 12;
    items[13].Flags = DIF_CENTERGROUP;
    items[13].Data = L"Отмена";

    HANDLE dialog = OpenGuardedDialog(items, 14, 59, 15, 13, PasswordDialogGuid);
    if (!dialog || dialog == INVALID_HANDLE_VALUE) {
        Say(L"Не удалось открыть диалог пароля.");
        return false;
    }
    const intptr_t result = g_info.DialogRun(dialog);
    const bool saved = result == 12;
    if (saved) {
        auto text = [&](int id, wchar_t* buffer, size_t size) {
            const wchar_t* value = reinterpret_cast<const wchar_t*>(g_info.SendDlgMessage(dialog, 63, id, nullptr));
            if (value) wcsncpy_s(buffer, size, value, _TRUNCATE);
        };
        text(2, name, std::size(name));
        text(4, user, std::size(user));
        text(6, secret, std::size(secret));
        text(8, host, std::size(host));
        text(10, note, std::size(note));
    }
    g_info.DialogFree(dialog);
    if (!saved) return false;
    if (name[0] == L'\0') {
        Say(L"Нужно имя записи.");
        return false;
    }
    item.name = name;
    item.user = user;
    item.secret = secret;
    item.host = host;
    item.note = note;
    return true;
}

bool EditSyncSettings(std::wstring& baseUrl, std::wstring& token) {
    wchar_t url[512]{};
    wchar_t pat[512]{};
    wcsncpy_s(url, baseUrl.c_str(), _TRUNCATE);
    wcsncpy_s(pat, token.c_str(), _TRUNCATE);
    FarDialogItem items[8]{};
    items[0].Type = DI_DOUBLEBOX;
    items[0].X1 = 0;
    items[0].Y1 = 0;
    items[0].X2 = 62;
    items[0].Y2 = 9;
    items[0].Data = L" NoteKeeper · HTTPS, PAT: vault ";
    items[1].Type = DI_TEXT;
    items[1].X1 = 2;
    items[1].Y1 = 2;
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
    HANDLE dialog = FarOpenDialog(g_info, MainGuid, DialogGuid, items, 8, 63, 10);
    if (!dialog || dialog == INVALID_HANDLE_VALUE) return false;
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
    std::wstring error;
    if (!NkTestConnection(nk, L"/vault/items", error)) {
        Say(error.c_str());
        return false;
    }
    return true;
}

bool EditKeyMeta(KeyRecord& item, bool create) {
    wchar_t name[128]{};
    wchar_t comment[256]{};
    wcsncpy_s(name, item.name.c_str(), _TRUNCATE);
    wcsncpy_s(comment, item.comment.c_str(), _TRUNCATE);
    FarDialogItem items[8]{};
    items[0].Type = DI_DOUBLEBOX;
    items[0].X1 = 0;
    items[0].Y1 = 0;
    items[0].X2 = 58;
    items[0].Y2 = 8;
    items[0].Data = create ? L" Новый ключ " : L" Ключ ";
    items[1].Type = DI_TEXT;
    items[1].X1 = 2;
    items[1].Y1 = 2;
    items[1].Data = L"Имя:";
    items[2].Type = DI_EDIT;
    items[2].X1 = 14;
    items[2].Y1 = 2;
    items[2].X2 = 56;
    items[2].Y2 = 2;
    items[2].Data = name;
    items[2].MaxLength = 127;
    items[2].Flags = DIF_FOCUS;
    items[3].Type = DI_TEXT;
    items[3].X1 = 2;
    items[3].Y1 = 4;
    items[3].Data = L"Коммент:";
    items[4].Type = DI_EDIT;
    items[4].X1 = 14;
    items[4].Y1 = 4;
    items[4].X2 = 56;
    items[4].Y2 = 4;
    items[4].Data = comment;
    items[4].MaxLength = 255;
    items[5].Type = DI_TEXT;
    items[5].Y1 = 6;
    items[5].Flags = DIF_SEPARATOR;
    items[6].Type = DI_BUTTON;
    items[6].X1 = 16;
    items[6].Y1 = 7;
    items[6].Flags = DIF_DEFAULTBUTTON | DIF_CENTERGROUP;
    items[6].Data = L"Сохранить";
    items[7].Type = DI_BUTTON;
    items[7].X1 = 30;
    items[7].Y1 = 7;
    items[7].Flags = DIF_CENTERGROUP;
    items[7].Data = L"Отмена";
    HANDLE dialog = OpenGuardedDialog(items, 8, 59, 9, 7, KeyDialogGuid);
    if (!dialog || dialog == INVALID_HANDLE_VALUE) {
        Say(L"Не удалось открыть диалог ключа.");
        return false;
    }
    const intptr_t result = g_info.DialogRun(dialog);
    const bool saved = result == 6;
    if (saved) {
        const wchar_t* n = reinterpret_cast<const wchar_t*>(g_info.SendDlgMessage(dialog, 63, 2, nullptr));
        const wchar_t* c = reinterpret_cast<const wchar_t*>(g_info.SendDlgMessage(dialog, 63, 4, nullptr));
        if (n) wcsncpy_s(name, n, _TRUNCATE);
        if (c) wcsncpy_s(comment, c, _TRUNCATE);
    }
    g_info.DialogFree(dialog);
    if (!saved) return false;
    if (name[0] == L'\0') {
        Say(L"Нужно имя ключа.");
        return false;
    }
    item.name = name;
    item.comment = comment;
    return true;
}
