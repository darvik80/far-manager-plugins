#include "ui.hpp"
#include "guid.hpp"
#include "nk_text.hpp"
#include "far/form.hpp"

PluginStartupInfo g_info{};

namespace {

HANDLE OpenGuardedDialog(FarDialogItem* items, size_t count, int width, int height, intptr_t cancelId,
                         const GUID& dialogId = DialogGuid) {
    return farform::OpenGuarded(g_info, MainGuid, dialogId, items, count, width, height, cancelId);
}

}  // namespace

void EatLeftoverHotkeys() {
    for (int i = 0; i < 40 &&
         !((GetAsyncKeyState(VK_F4) & 0x8000) == 0 &&
           (GetAsyncKeyState(VK_F7) & 0x8000) == 0 &&
           (GetAsyncKeyState(VK_RETURN) & 0x8000) == 0); ++i)
        Sleep(15);
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
    return farform::AskText(g_info, MainGuid, DialogGuid, title, prompt, password);
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
    farform::FillBox(items[0], farform::kWidth, 15, todo.id.empty() ? L" Новый todo " : L" Todo ");
    farform::FillLabel(items[1], 2, L"Имя:");
    farform::FillEdit(items[2], 2, farform::kEditX, farform::kInnerR, title, 255, false, DIF_FOCUS);
    farform::FillLabel(items[3], 4, L"Повтор:");
    items[4] = {};
    items[4].Type = DI_RADIOBUTTON;
    items[4].X1 = farform::kEditX;
    items[4].Y1 = 4;
    items[4].Flags = DIF_GROUP;
    items[4].Param.Selected = (repeat == L"none" || repeat.empty()) ? 1 : 0;
    items[4].Data = L"Нет";
    items[5] = {};
    items[5].Type = DI_RADIOBUTTON;
    items[5].X1 = 24;
    items[5].Y1 = 4;
    items[5].Param.Selected = repeat == L"daily" ? 1 : 0;
    items[5].Data = L"День";
    items[6] = {};
    items[6].Type = DI_RADIOBUTTON;
    items[6].X1 = 36;
    items[6].Y1 = 4;
    items[6].Param.Selected = repeat == L"weekly" ? 1 : 0;
    items[6].Data = L"Нед.";
    items[7] = {};
    items[7].Type = DI_RADIOBUTTON;
    items[7].X1 = 48;
    items[7].Y1 = 4;
    items[7].Param.Selected = repeat == L"monthly" ? 1 : 0;
    items[7].Data = L"Мес.";
    farform::FillLabel(items[8], 6, L"Напомнить:");
    farform::FillEdit(items[9], 6, farform::kEditX, farform::kInnerR, reminder, 31, false);
    farform::FillLabel(items[10], 8, L"Каналы:");
    items[11] = {};
    items[11].Type = DI_CHECKBOX;
    items[11].X1 = farform::kEditX;
    items[11].Y1 = 8;
    items[11].Param.Selected = telegram ? 1 : 0;
    items[11].Data = L"Telegram";
    items[12] = {};
    items[12].Type = DI_CHECKBOX;
    items[12].X1 = 32;
    items[12].Y1 = 8;
    items[12].Param.Selected = dingtalk ? 1 : 0;
    items[12].Data = L"DingTalk";
    farform::FillSeparator(items[13], 11);
    farform::FillButton(items[14], 12, L"Текст", true);
    farform::FillButton(items[15], 12, L"Отмена", false);

    HANDLE dialog = OpenGuardedDialog(items, 16, farform::kWidth, 15, 15, TodoDialogGuid);
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
    farform::FillBox(items[0], farform::kWidth, 10, L" NoteKeeper · HTTPS ");
    farform::FillLabel(items[1], 2, L"URL:");
    farform::FillEdit(items[2], 2, farform::kEditX, farform::kInnerR, url, 511, false, DIF_FOCUS);
    farform::FillLabel(items[3], 4, L"PAT:");
    farform::FillEdit(items[4], 4, farform::kEditX, farform::kInnerR, pat, 511, true);
    farform::FillSeparator(items[5], 6);
    farform::FillButton(items[6], 8, L"OK", true);
    farform::FillButton(items[7], 8, L"Отмена", false);
    HANDLE dialog = OpenGuardedDialog(items, 8, farform::kWidth, 10, 7);
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
    if (!NkTestConnection(nk, L"/notes?isArchived=false&isDeleted=false", notesError) &&
        !NkTestConnection(nk, L"/todos?isArchived=false&isDeleted=false", todosError)) {
        Say(notesError.c_str());
        return false;
    }
    return true;
}
