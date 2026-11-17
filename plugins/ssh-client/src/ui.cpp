#include "ui.hpp"
#include "guid.hpp"
#include "far/form.hpp"

PluginStartupInfo g_info{};

HANDLE OpenGuardedDialog(FarDialogItem* items, size_t count, intptr_t width, intptr_t height, intptr_t cancelId,
                         const GUID& dialogId) {
    return farform::OpenGuarded(g_info, MainGuid, dialogId, items, count, width, height, cancelId);
}

namespace {

std::wstring WrapLine(const std::wstring& line, size_t width) {
    std::wstring out;
    size_t col = 0;
    size_t i = 0;
    while (i < line.size()) {
        size_t next = line.find(L' ', i);
        if (next == std::wstring::npos) next = line.size();
        const size_t len = next - i;
        if (col > 0 && col + 1 + len > width) {
            out += L'\n';
            col = 0;
        }
        if (col > 0) {
            out += L' ';
            ++col;
        }
        out.append(line, i, len);
        col += len;
        i = next;
        while (i < line.size() && line[i] == L' ') ++i;
    }
    return out;
}

// FAR clips a Message line at the box border instead of wrapping it, so long errors lose their tail
std::wstring WrapText(const std::wstring& text, size_t width) {
    std::wstring out;
    size_t start = 0;
    for (;;) {
        const size_t nl = text.find(L'\n', start);
        const size_t stop = nl == std::wstring::npos ? text.size() : nl;
        if (start > 0) out += L'\n';
        out += WrapLine(text.substr(start, stop - start), width);
        if (nl == std::wstring::npos) break;
        start = nl + 1;
    }
    return out;
}

}  // namespace

void Say(const wchar_t* text) {
    if (!g_info.Message) return;
    std::wstring blob = L"SSH\n\n";
    if (text && *text) blob += WrapText(text, 86);
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
