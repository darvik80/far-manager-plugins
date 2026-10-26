#include "clipboard.hpp"

#include <windows.h>

namespace {

DWORD WINAPI ClearLater(void* param) {
    auto* text = static_cast<std::wstring*>(param);
    Sleep(30000);
    if (OpenClipboard(nullptr)) {
        HANDLE data = GetClipboardData(CF_UNICODETEXT);
        if (data) {
            const wchar_t* current = static_cast<const wchar_t*>(GlobalLock(data));
            if (current && text && *text == current) EmptyClipboard();
            if (current) GlobalUnlock(data);
        }
        CloseClipboard();
    }
    delete text;
    return 0;
}

}  // namespace

bool CopySecret(const std::wstring& text, std::wstring& error) {
    if (!OpenClipboard(nullptr)) {
        error = L"Не удалось открыть буфер обмена.";
        return false;
    }
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        error = L"Не удалось выделить буфер обмена.";
        return false;
    }
    void* dest = GlobalLock(mem);
    memcpy(dest, text.c_str(), bytes);
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        error = L"Не удалось записать буфер обмена.";
        return false;
    }
    CloseClipboard();
    HANDLE thread = CreateThread(nullptr, 0, ClearLater, new std::wstring(text), 0, nullptr);
    if (thread) CloseHandle(thread);
    return true;
}
