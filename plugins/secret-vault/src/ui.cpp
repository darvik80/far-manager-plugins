#include "ui.hpp"
#include "guid.hpp"
#include "far/form.hpp"
#include "nk_api.hpp"

#include <vector>

PluginStartupInfo g_info{};

namespace {

HANDLE OpenGuardedDialog(FarDialogItem* items, size_t count, intptr_t width, intptr_t height,
                         intptr_t cancelId, const GUID& dialogId = DialogGuid) {
    return farform::OpenGuarded(g_info, MainGuid, dialogId, items, count, width, height, cancelId);
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
    return farform::AskText(g_info, MainGuid, DialogGuid, title, prompt, password);
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
    farform::FillBox(items[0], farform::kWidth, 15, L" Новый ключ ");
    farform::FillLabel(items[1], 2, L"Имя:");
    farform::FillEdit(items[2], 2, farform::kEditX, farform::kInnerR, name, 127, false, DIF_FOCUS);
    farform::FillLabel(items[3], 4, L"Коммент:");
    farform::FillEdit(items[4], 4, farform::kEditX, farform::kInnerR, comment, 255, false);
    farform::FillLabel(items[5], 6, L"Тип:");
    items[6] = {};
    items[6].Type = DI_RADIOBUTTON;
    items[6].X1 = farform::kEditX;
    items[6].Y1 = 6;
    items[6].X2 = farform::kInnerR;
    items[6].Flags = DIF_GROUP;
    items[6].Param.Selected = 1;
    items[6].Data = L"создать ed25519";
    items[7] = {};
    items[7].Type = DI_RADIOBUTTON;
    items[7].X1 = farform::kEditX;
    items[7].Y1 = 7;
    items[7].X2 = farform::kInnerR;
    items[7].Data = L"создать RSA 3072";
    items[8] = {};
    items[8].Type = DI_RADIOBUTTON;
    items[8].X1 = farform::kEditX;
    items[8].Y1 = 8;
    items[8].X2 = farform::kInnerR;
    items[8].Data = L"импорт OpenSSH-файла";
    farform::FillSeparator(items[9], 11);
    farform::FillButton(items[10], 12, L"Создать", true);
    farform::FillButton(items[11], 12, L"Отмена", false);

    HANDLE dialog = OpenGuardedDialog(items, 12, farform::kWidth, 15, 11, KeyCreateDialogGuid);
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
    farform::FillBox(items[0], farform::kWidth, 15, create ? L" Новый пароль " : L" Пароль ");
    farform::FillLabel(items[1], 2, L"Имя:");
    farform::FillEdit(items[2], 2, farform::kEditX, farform::kInnerR, name, 127, false, DIF_FOCUS);
    farform::FillLabel(items[3], 4, L"Логин:");
    farform::FillEdit(items[4], 4, farform::kEditX, 32, user, 127, false);
    farform::FillLabel(items[7], 4, L"Хост:", 35);
    farform::FillEdit(items[8], 4, 42, farform::kInnerR, host, 255, false);
    farform::FillLabel(items[5], 6, L"Пароль:");
    farform::FillEdit(items[6], 6, farform::kEditX, farform::kInnerR, secret, 255, true);
    farform::FillLabel(items[9], 8, L"Заметка:");
    farform::FillEdit(items[10], 8, farform::kEditX, farform::kInnerR, note, 255, false);
    farform::FillSeparator(items[11], 11);
    farform::FillButton(items[12], 12, L"Сохранить", true);
    farform::FillButton(items[13], 12, L"Отмена", false);

    HANDLE dialog = OpenGuardedDialog(items, 14, farform::kWidth, 15, 13, PasswordDialogGuid);
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
    farform::FillBox(items[0], farform::kWidth, 10, L" NoteKeeper · HTTPS, PAT: vault ");
    farform::FillLabel(items[1], 2, L"URL:");
    farform::FillEdit(items[2], 2, farform::kEditX, farform::kInnerR, url, 511, false, DIF_FOCUS);
    farform::FillLabel(items[3], 4, L"PAT:");
    farform::FillEdit(items[4], 4, farform::kEditX, farform::kInnerR, pat, 511, true);
    farform::FillSeparator(items[5], 6);
    farform::FillButton(items[6], 8, L"OK", true);
    farform::FillButton(items[7], 8, L"Отмена", false);
    HANDLE dialog = OpenGuardedDialog(items, 8, farform::kWidth, 10, 7, DialogGuid);
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
    farform::FillBox(items[0], farform::kWidth, 9, create ? L" Новый ключ " : L" Ключ ");
    farform::FillLabel(items[1], 2, L"Имя:");
    farform::FillEdit(items[2], 2, farform::kEditX, farform::kInnerR, name, 127, false, DIF_FOCUS);
    farform::FillLabel(items[3], 4, L"Коммент:");
    farform::FillEdit(items[4], 4, farform::kEditX, farform::kInnerR, comment, 255, false);
    farform::FillSeparator(items[5], 5);
    farform::FillButton(items[6], 7, L"Сохранить", true);
    farform::FillButton(items[7], 7, L"Отмена", false);
    HANDLE dialog = OpenGuardedDialog(items, 8, farform::kWidth, 9, 7, KeyDialogGuid);
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
