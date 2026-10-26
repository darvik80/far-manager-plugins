#include "ui.hpp"
#include "guid.hpp"

#include <vector>

PluginStartupInfo g_info{};

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
    HANDLE dialog = g_info.DialogInit(MainGuid, MenuGuid, -1, -1, 56, 7, nullptr, items, 4, 0, 0, nullptr, 0);
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

    HANDLE dialog = g_info.DialogInit(MainGuid, MenuGuid, -1, -1, 59, 15, nullptr, items, 12, 0, 0, nullptr, 0);
    if (!dialog || dialog == INVALID_HANDLE_VALUE) return false;
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
    items[0].Y2 = 16;
    items[0].Data = create ? L" Новый пароль " : L" Пароль ";

    const wchar_t* labels[] = { L"Имя:", L"Логин:", L"Пароль:", L"Хост:", L"Заметка:" };
    wchar_t* buffers[] = { name, user, secret, host, note };
    const size_t sizes[] = { std::size(name), std::size(user), std::size(secret), std::size(host), std::size(note) };
    for (int i = 0; i < 5; ++i) {
        const int y = 2 + i * 2;
        items[1 + i * 2].Type = DI_TEXT;
        items[1 + i * 2].X1 = 2;
        items[1 + i * 2].Y1 = y;
        items[1 + i * 2].Data = labels[i];
        items[2 + i * 2].Type = i == 2 ? DI_PSWEDIT : DI_EDIT;
        items[2 + i * 2].X1 = 14;
        items[2 + i * 2].Y1 = y;
        items[2 + i * 2].X2 = 56;
        items[2 + i * 2].Y2 = y;
        items[2 + i * 2].Data = buffers[i];
        items[2 + i * 2].MaxLength = static_cast<intptr_t>(sizes[i] - 1);
        if (i == 0) items[2 + i * 2].Flags = DIF_FOCUS;
    }

    items[11].Type = DI_TEXT;
    items[11].Y1 = 12;
    items[11].Flags = DIF_SEPARATOR;

    items[12].Type = DI_BUTTON;
    items[12].X1 = 16;
    items[12].Y1 = 14;
    items[12].Flags = DIF_DEFAULTBUTTON | DIF_CENTERGROUP;
    items[12].Data = L"Сохранить";
    items[13].Type = DI_BUTTON;
    items[13].X1 = 30;
    items[13].Y1 = 14;
    items[13].Flags = DIF_CENTERGROUP;
    items[13].Data = L"Отмена";

    HANDLE dialog = g_info.DialogInit(MainGuid, MenuGuid, -1, -1, 59, 17, nullptr, items, 14, 0, 0, nullptr, 0);
    if (!dialog || dialog == INVALID_HANDLE_VALUE) return false;
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
    HANDLE dialog = g_info.DialogInit(MainGuid, MenuGuid, -1, -1, 59, 9, nullptr, items, 8, 0, 0, nullptr, 0);
    if (!dialog || dialog == INVALID_HANDLE_VALUE) return false;
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
