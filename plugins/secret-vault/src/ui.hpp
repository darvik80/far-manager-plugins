#ifndef SECRET_VAULT_UI_HPP_
#define SECRET_VAULT_UI_HPP_

#include "far/plugin.hpp"
#include "vault.hpp"

#include <string>
#include <vector>

extern PluginStartupInfo g_info;

void Say(const wchar_t* text);
bool AskYes(const wchar_t* text);
std::wstring AskText(const wchar_t* title, const wchar_t* prompt, bool password);
intptr_t Pick(const wchar_t* title, const std::vector<std::wstring>& lines);
bool EditPassword(PasswordRecord& item, bool create);
bool EditKeyMeta(KeyRecord& item, bool create);
bool CreateKey(KeyRecord& item, std::wstring& kind);
std::wstring AskFile(const wchar_t* title, bool save);

#endif
