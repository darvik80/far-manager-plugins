#include "ui.hpp"
#include "guid.hpp"
#include "far/form.hpp"

PluginStartupInfo g_info{};

HANDLE OpenGuardedDialog(FarDialogItem* items, size_t count, int width, int height, intptr_t cancelId) {
    return farform::OpenGuarded(g_info, MainGuid, DialogGuid, items, count, width, height, cancelId);
}

void Say(const wchar_t* text) {
    if (!g_info.Message) return;
    std::wstring blob = L"SSH\n\n";
    if (text && *text) blob += text;
    g_info.Message(MainGuid, DialogGuid, FMSG_WARNING | FMSG_MB_OK | FMSG_ALLINONE, nullptr,
                   reinterpret_cast<const wchar_t* const*>(blob.c_str()), 0, 0);
}

bool AskYes(const wchar_t* text) {
    const wchar_t* items[] = { L"SSH", L"", text };
    if (!g_info.Message) return false;
    return g_info.Message(MainGuid, MenuGuid, FMSG_WARNING | FMSG_MB_YESNO, nullptr, items, 3, 2) == 0;
}

std::wstring AskText(const wchar_t* title, const wchar_t* prompt, bool password) {
    return farform::AskText(g_info, MainGuid, DialogGuid, title, prompt, password);
}
