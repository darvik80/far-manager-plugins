#ifndef NK_API_HPP_
#define NK_API_HPP_

#include "nk_settings.hpp"

#include <string>
#include <vector>

struct NkVaultItem {
    std::wstring id;
    std::wstring type;
    std::wstring title;
    std::wstring username;
    std::wstring host;
    std::wstring url;
    std::wstring password;
    std::wstring privateKey;
    std::wstring secret;
    std::wstring updatedAt;
    int revision = 1;
};

struct NkNote {
    std::wstring id;
    std::wstring title;
    std::wstring content;
    std::wstring folder;
    std::wstring priority;
    bool favorite = false;
    bool encrypted = false;
    std::vector<std::wstring> tags;
    std::wstring updatedAt;
};

struct NkTodo {
    std::wstring id;
    std::wstring title;
    std::wstring description;
    bool completed = false;
    std::wstring priority;
    std::vector<std::wstring> tags;
    std::wstring reminder;
    std::wstring notificationChannels;
    std::wstring repeat;
    std::wstring scheduleEndDate;
    std::wstring dueDate;
    std::wstring updatedAt;
};

bool NkTestConnection(const NkSettings& settings, const wchar_t* apiPath, std::wstring& error);
bool NkListVaultItems(std::vector<NkVaultItem>& items, std::wstring& error);
std::wstring NkLookupPassword(const std::wstring& title);
std::wstring NkLookupPasswordForHost(const std::wstring& host, const std::wstring& user);
std::wstring NkLookupKeyFile(const std::wstring& title, std::wstring& error);
std::wstring NkLookupKeyPem(const std::wstring& title, std::wstring& error);
bool NkStorePassword(const std::wstring& title, const std::wstring& secret, const std::wstring& host,
                     const std::wstring& user, std::wstring& error);
bool NkStoreKey(const std::wstring& title, const std::wstring& pem, std::wstring& error);

bool NkListNotes(std::vector<NkNote>& notes, std::wstring& error);
bool NkGetNote(const std::wstring& id, NkNote& note, std::wstring& error);
bool NkSaveNote(NkNote& note, std::wstring& error);
bool NkDeleteNote(const std::wstring& id, std::wstring& error);

bool NkListTodos(std::vector<NkTodo>& todos, std::wstring& error);
bool NkGetTodo(const std::wstring& id, NkTodo& todo, std::wstring& error);
bool NkSaveTodo(NkTodo& todo, std::wstring& error);
bool NkDeleteTodo(const std::wstring& id, std::wstring& error);
bool NkToggleTodo(const std::wstring& id, NkTodo& todo, std::wstring& error);

#endif
