#include "ui.hpp"
#include "guid.hpp"

PluginStartupInfo g_info{};

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

    HANDLE dialog = g_info.DialogInit(
        MainGuid, MenuGuid, -1, -1, 56, 7, nullptr, items, 4, 0, 0, nullptr, 0);
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
