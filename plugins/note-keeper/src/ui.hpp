#ifndef NOTE_KEEPER_UI_HPP_
#define NOTE_KEEPER_UI_HPP_

#include "far/plugin.hpp"
#include "nk_api.hpp"

#include <string>

extern PluginStartupInfo g_info;

void Say(const wchar_t* text);
bool AskYes(const wchar_t* text);
void EatLeftoverHotkeys();
std::wstring AskText(const wchar_t* title, const wchar_t* prompt, bool password);
bool EditTodoMeta(NkTodo& todo);
bool EditSyncSettings(std::wstring& baseUrl, std::wstring& token);

#endif
