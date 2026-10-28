#ifndef SSH_CLIENT_UI_HPP_
#define SSH_CLIENT_UI_HPP_

#include "far/plugin.hpp"
#include "guid.hpp"

#include <string>

extern PluginStartupInfo g_info;

void Say(const wchar_t* text);
bool AskYes(const wchar_t* text);
std::wstring AskText(const wchar_t* title, const wchar_t* prompt, bool password);
HANDLE OpenGuardedDialog(FarDialogItem* items, size_t count, intptr_t width, intptr_t height, intptr_t cancelId,
                         const GUID& dialogId = DialogGuid);

#endif
