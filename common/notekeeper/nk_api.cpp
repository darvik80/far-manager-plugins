#include "nk_api.hpp"
#include "nk_http.hpp"
#include "nk_json.hpp"
#include "nk_secure.hpp"
#include "nk_text.hpp"

#include <windows.h>
#include <cstdio>

#include <algorithm>
#include <sstream>

namespace {

std::wstring Fail(std::wstring& error, const wchar_t* text, int status = 0, unsigned long winError = 0) {
    error = text;
    if (status == 401) error += L" — неверный PAT.";
    else if (status == 403) error += L" — у PAT нет нужного scope.";
    else if (status) error += L" (" + std::to_wstring(status) + L")";
    else if (winError) error += L" (WinHTTP " + std::to_wstring(winError) + L")";
    return {};
}

bool NeedSettings(NkSettings& settings, std::wstring& error) {
    NkLoadSettings(settings);
    if (NkConfigured(settings)) return true;
    error = L"Укажи URL и PAT в настройках NoteKeeper / Secret Vault.";
    return false;
}

std::wstring Api(const NkSettings& settings, const std::wstring& path) {
    return NkNormalizeUrl(settings.baseUrl) + L"/api/v1" + path;
}

std::string IdArgs(const std::wstring& id) {
    return std::string("{\"id\":\"") + NkJsonEscape(id) + "\"}";
}

bool McpTool(const NkSettings& settings, const char* name, const std::string& args,
             std::string& payload, std::wstring& error) {
    const std::string body = std::string("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"")
        + name + "\",\"arguments\":" + (args.empty() ? "{}" : args) + "}}";
    const NkHttpResult res = NkRequest(L"POST", NkNormalizeUrl(settings.baseUrl) + L"/mcp", settings.token, body);
    if (res.status != 200) {
        Fail(error, L"MCP запрос не удался", res.status, res.winError);
        return false;
    }
    return NkMcpPayload(res.body, payload, error);
}

NkVaultItem VaultFrom(const NkObject& obj) {
    NkVaultItem item;
    item.id = obj.S("id");
    item.type = obj.S("type");
    item.title = obj.S("title");
    item.username = obj.S("username");
    item.host = obj.S("host");
    item.url = obj.S("url");
    item.password = obj.S("password");
    item.privateKey = obj.S("privateKey");
    item.secret = obj.S("secret");
    item.revision = obj.N("revision", 1);
    return item;
}

NkNote NoteFrom(const NkObject& obj) {
    NkNote note;
    note.id = obj.S("id");
    note.title = obj.S("title");
    note.content = obj.S("content");
    note.folder = obj.S("folder");
    note.priority = obj.S("priority");
    note.favorite = obj.B("isFavorite") || obj.B("favorite");
    note.encrypted = obj.B("isEncrypted") || obj.B("encrypted");
    note.tags = obj.A("tags");
    note.updatedAt = obj.S("updatedAt");
    if (note.updatedAt.empty()) note.updatedAt = obj.S("updated_at");
    if (note.updatedAt.empty()) note.updatedAt = obj.S("modifiedAt");
    if (note.updatedAt.empty()) note.updatedAt = obj.S("createdAt");
    return note;
}

NkTodo TodoFrom(const NkObject& obj) {
    NkTodo todo;
    todo.id = obj.S("id");
    todo.title = obj.S("title");
    todo.description = obj.S("description");
    todo.completed = obj.B("completed");
    todo.priority = obj.S("priority");
    todo.tags = obj.A("tags");
    todo.reminder = obj.S("reminder");
    todo.notificationChannels = obj.S("notificationChannels");
    todo.repeat = obj.S("scheduleRepeat");
    const auto schedule = obj.objects.find("schedule");
    if (schedule != obj.objects.end()) {
        if (todo.repeat.empty()) todo.repeat = schedule->second.S("repeat");
        todo.scheduleEndDate = schedule->second.S("endDate");
    }
    if (todo.repeat.empty()) todo.repeat = L"none";
    todo.dueDate = obj.S("dueDate");
    todo.updatedAt = obj.S("updatedAt");
    if (todo.updatedAt.empty()) todo.updatedAt = obj.S("updated_at");
    if (todo.updatedAt.empty()) todo.updatedAt = obj.S("modifiedAt");
    if (todo.updatedAt.empty()) todo.updatedAt = obj.S("createdAt");
    return todo;
}

bool ParseOne(const std::string& json, NkObject& obj) {
    size_t i = 0;
    return NkParseObject(json, i, obj, true);
}

std::string NoteJson(const NkNote& note) {
    std::ostringstream body;
    body << "{\"title\":\"" << NkJsonEscape(note.title)
         << "\",\"content\":\"" << NkJsonEscape(note.content)
         << "\",\"folder\":\"" << NkJsonEscape(note.folder.empty() ? L"default" : note.folder)
         << "\",\"priority\":\"" << NkJsonEscape(note.priority.empty() ? L"medium" : note.priority)
         << "\",\"isFavorite\":" << (note.favorite ? "true" : "false")
         << ",\"isEncrypted\":" << (note.encrypted ? "true" : "false")
         << ",\"tags\":" << NkJsonStringArray(note.tags) << "}";
    return body.str();
}

std::string TodoJson(const NkTodo& todo) {
    std::ostringstream body;
    body << "{\"title\":\"" << NkJsonEscape(todo.title)
         << "\",\"description\":\"" << NkJsonEscape(todo.description)
         << "\",\"completed\":" << (todo.completed ? "true" : "false")
         << ",\"priority\":\"" << NkJsonEscape(todo.priority.empty() ? L"medium" : todo.priority)
         << "\",\"tags\":" << NkJsonStringArray(todo.tags);
    if (!todo.dueDate.empty())
        body << ",\"dueDate\":\"" << NkJsonEscape(todo.dueDate) << "\"";
    if (!todo.reminder.empty())
        body << ",\"reminder\":\"" << NkJsonEscape(todo.reminder) << "\"";
    if (!todo.notificationChannels.empty())
        body << ",\"notificationChannels\":\"" << NkJsonEscape(todo.notificationChannels) << "\"";
    const std::wstring repeat = todo.repeat.empty() ? L"none" : todo.repeat;
    body << ",\"schedule\":{\"repeat\":\"" << NkJsonEscape(repeat) << "\"";
    if (!todo.scheduleEndDate.empty() && NkLower(repeat) != L"none")
        body << ",\"endDate\":\"" << NkJsonEscape(todo.scheduleEndDate) << "\"";
    body << "}";
    body << "}";
    return body.str();
}

bool HostMatch(const NkVaultItem& item, const std::wstring& host) {
    const std::wstring want = NkExtractHost(host);
    if (want.empty()) return false;
    if (NkExtractHost(item.host) == want) return true;
    const std::wstring fromUrl = NkExtractHost(item.url);
    return !fromUrl.empty() && fromUrl == want;
}

std::wstring WriteTempPem(const std::wstring& pem, std::wstring& error) {
    const std::wstring path = NkSecureTempPath(L"FarNoteKeeper", L"key", L".pem", error);
    if (path.empty()) return {};
    const std::string bytes = NkUtf8(pem);
    if (!NkWriteSecureFile(path, bytes.data(), bytes.size())) {
        error = L"Не удалось записать временный ключ.";
        DeleteFileW(path.c_str());
        return {};
    }
    return path;
}

}  // namespace

bool NkTestConnection(const NkSettings& settings, const wchar_t* apiPath, std::wstring& error) {
    error.clear();
    const std::wstring base = NkNormalizeUrl(settings.baseUrl);
    if (base.empty()) {
        error = L"Укажи URL сервера.";
        return false;
    }
    if (!NkUrlIsSecure(base)) {
        error = L"Нужен HTTPS (http:// только для localhost).";
        return false;
    }
    if (settings.token.empty()) {
        error = L"Укажи PAT.";
        return false;
    }
    const std::wstring path = (apiPath && apiPath[0]) ? apiPath : L"/vault/items";
    const NkHttpResult res = NkRequest(L"GET", base + L"/api/v1" + path, settings.token);
    if (res.status == 200) return true;
    error = L"Нет связи с NoteKeeper";
    if (res.status == 401) error += L" — неверный PAT.";
    else if (res.status == 403) {
        if (path.find(L"vault") != std::wstring::npos) error += L" — в PAT нет scope vault.";
        else if (path.find(L"todo") != std::wstring::npos) error += L" — в PAT нет scope todos.";
        else if (path.find(L"note") != std::wstring::npos) error += L" — в PAT нет scope notes.";
        else error += L" — у PAT нет нужного scope.";
    } else if (res.status > 0) {
        error += L" (" + std::to_wstring(res.status) + L").";
    } else if (res.winError) {
        error += L" (WinHTTP " + std::to_wstring(res.winError) + L"). Проверь URL.";
    } else {
        error += L". Проверь URL.";
    }
    return false;
}

bool NkListVaultItems(std::vector<NkVaultItem>& items, std::wstring& error) {
    items.clear();
    NkSettings settings;
    if (!NeedSettings(settings, error)) return false;
    const NkHttpResult res = NkRequest(L"GET", Api(settings, L"/vault/items"), settings.token);
    if (res.status != 200) {
        Fail(error, L"Не удалось прочитать vault NoteKeeper", res.status, res.winError);
        return false;
    }
    std::vector<NkObject> objects;
    if (!NkParseObjectArray(res.body, objects)) {
        error = L"Сервер вернул непонятный JSON vault.";
        return false;
    }
    for (const auto& obj : objects) {
        auto item = VaultFrom(obj);
        if (!item.id.empty()) items.push_back(std::move(item));
    }
    return true;
}

std::wstring NkLookupPassword(const std::wstring& title) {
    if (title.empty()) return {};
    std::vector<NkVaultItem> items;
    std::wstring error;
    if (!NkListVaultItems(items, error)) return {};
    const std::wstring key = NkLower(title);
    for (const auto& item : items) {
        if (NkLower(item.title) != key) continue;
        if (!item.password.empty()) return item.password;
        if (!item.secret.empty()) return item.secret;
    }
    return {};
}

std::wstring NkLookupPasswordForHost(const std::wstring& host, const std::wstring& user) {
    std::vector<NkVaultItem> items;
    std::wstring error;
    if (!NkListVaultItems(items, error)) return {};
    const std::wstring wantUser = NkLower(user);
    const NkVaultItem* fallback = nullptr;
    for (const auto& item : items) {
        if (item.type == L"ssh_key") continue;
        if (!HostMatch(item, host)) continue;
        const std::wstring have = NkLower(item.username);
        if (!wantUser.empty() && !have.empty() && have != wantUser) continue;
        const std::wstring secret = item.password.empty() ? item.secret : item.password;
        if (secret.empty()) continue;
        if (wantUser.empty() || have == wantUser || have.empty()) {
            if (!wantUser.empty() && have == wantUser) return secret;
            if (!fallback) fallback = &item;
        }
    }
    if (!fallback) return {};
    return fallback->password.empty() ? fallback->secret : fallback->password;
}

std::wstring NkLookupKeyFile(const std::wstring& title, std::wstring& error) {
    if (title.empty()) return {};
    std::vector<NkVaultItem> items;
    if (!NkListVaultItems(items, error)) return {};
    const std::wstring key = NkLower(title);
    for (const auto& item : items) {
        if (NkLower(item.title) != key) continue;
        if (item.privateKey.empty()) continue;
        return WriteTempPem(item.privateKey, error);
    }
    error = L"Ключ не найден в NoteKeeper vault: " + title;
    return {};
}

bool NkStorePassword(const std::wstring& title, const std::wstring& secret, const std::wstring& host,
                     const std::wstring& user, std::wstring& error) {
    NkSettings settings;
    if (!NeedSettings(settings, error)) return false;
    std::vector<NkVaultItem> items;
    NkListVaultItems(items, error);
    error.clear();
    std::wstring id;
    for (const auto& item : items) {
        if (NkLower(item.title) == NkLower(title)) {
            id = item.id;
            break;
        }
    }
    std::ostringstream body;
    body << "{\"title\":\"" << NkJsonEscape(title)
         << "\",\"type\":\"password\""
         << ",\"username\":\"" << NkJsonEscape(user)
         << "\",\"host\":\"" << NkJsonEscape(host)
         << "\",\"password\":\"" << NkJsonEscape(secret) << "\"}";
    NkHttpResult res;
    if (id.empty()) {
        res = NkRequest(L"POST", Api(settings, L"/vault/items"), settings.token, body.str());
    } else {
        res = NkRequest(L"PUT", Api(settings, L"/vault/items/" + id), settings.token, body.str());
    }
    if (res.status < 200 || res.status >= 300) {
        Fail(error, L"Не удалось записать пароль в NoteKeeper", res.status, res.winError);
        return false;
    }
    return true;
}

bool NkStoreKey(const std::wstring& title, const std::wstring& pem, std::wstring& error) {
    NkSettings settings;
    if (!NeedSettings(settings, error)) return false;
    std::vector<NkVaultItem> items;
    NkListVaultItems(items, error);
    error.clear();
    std::wstring id;
    for (const auto& item : items) {
        if (NkLower(item.title) == NkLower(title)) {
            id = item.id;
            break;
        }
    }
    std::ostringstream body;
    body << "{\"title\":\"" << NkJsonEscape(title)
         << "\",\"type\":\"ssh_key\""
         << ",\"privateKey\":\"" << NkJsonEscape(pem) << "\"}";
    NkHttpResult res;
    if (id.empty()) {
        res = NkRequest(L"POST", Api(settings, L"/vault/items"), settings.token, body.str());
    } else {
        res = NkRequest(L"PUT", Api(settings, L"/vault/items/" + id), settings.token, body.str());
    }
    if (res.status < 200 || res.status >= 300) {
        Fail(error, L"Не удалось записать ключ в NoteKeeper", res.status, res.winError);
        return false;
    }
    return true;
}

bool NkListNotes(std::vector<NkNote>& notes, std::wstring& error) {
    notes.clear();
    NkSettings settings;
    if (!NeedSettings(settings, error)) return false;
    std::string payload;
    std::vector<NkObject> objects;
    if (McpTool(settings, "list_notes", "{}", payload, error) && NkParseObjectArray(payload, objects)) {
        for (const auto& obj : objects) notes.push_back(NoteFrom(obj));
        return true;
    }
    const NkHttpResult res = NkRequest(L"GET", Api(settings, L"/notes?isArchived=false&isDeleted=false"), settings.token);
    if (res.status != 200) {
        if (error.empty()) Fail(error, L"Не удалось получить notes", res.status, res.winError);
        return false;
    }
    objects.clear();
    if (!NkParseObjectArray(res.body, objects)) {
        error = L"Сервер вернул непонятный JSON notes.";
        return false;
    }
    for (const auto& obj : objects) notes.push_back(NoteFrom(obj));
    return true;
}

bool NkGetNote(const std::wstring& id, NkNote& note, std::wstring& error) {
    NkSettings settings;
    if (!NeedSettings(settings, error)) return false;
    std::string payload;
    NkObject obj;
    if (McpTool(settings, "get_note", IdArgs(id), payload, error) && ParseOne(payload, obj)) {
        note = NoteFrom(obj);
        return true;
    }
    const NkHttpResult res = NkRequest(L"GET", Api(settings, L"/notes/" + id), settings.token);
    if (res.status != 200) {
        if (error.empty()) Fail(error, L"Не удалось открыть note", res.status, res.winError);
        return false;
    }
    if (!ParseOne(res.body, obj)) {
        error = L"Сервер вернул непонятный JSON note.";
        return false;
    }
    note = NoteFrom(obj);
    return true;
}

bool NkSaveNote(NkNote& note, std::wstring& error) {
    NkSettings settings;
    if (!NeedSettings(settings, error)) return false;
    const char* tool = note.id.empty() ? "create_note" : "update_note";
    std::ostringstream args;
    args << "{\"title\":\"" << NkJsonEscape(note.title)
         << "\",\"content\":\"" << NkJsonEscape(note.content)
         << "\",\"folder\":\"" << NkJsonEscape(note.folder.empty() ? L"default" : note.folder) << "\"";
    if (!note.id.empty()) args << ",\"id\":\"" << NkJsonEscape(note.id) << "\"";
    args << "}";
    std::string payload;
    NkObject obj;
    if (McpTool(settings, tool, args.str(), payload, error) && ParseOne(payload, obj)) {
        note = NoteFrom(obj);
        return true;
    }
    NkHttpResult res;
    if (note.id.empty()) {
        res = NkRequest(L"POST", Api(settings, L"/notes"), settings.token, NoteJson(note));
    } else {
        res = NkRequest(L"PUT", Api(settings, L"/notes/" + note.id), settings.token, NoteJson(note));
    }
    if (res.status < 200 || res.status >= 300) {
        if (error.empty()) Fail(error, L"Не удалось сохранить note", res.status, res.winError);
        return false;
    }
    if (ParseOne(res.body, obj)) note = NoteFrom(obj);
    return true;
}

bool NkDeleteNote(const std::wstring& id, std::wstring& error) {
    NkSettings settings;
    if (!NeedSettings(settings, error)) return false;
    const NkHttpResult res = NkRequest(L"DELETE", Api(settings, L"/notes/" + id + L"?permanent=true"), settings.token);
    if (res.status != 204 && res.status != 200) {
        Fail(error, L"Не удалось удалить note", res.status, res.winError);
        return false;
    }
    return true;
}

bool NkListTodos(std::vector<NkTodo>& todos, std::wstring& error) {
    todos.clear();
    NkSettings settings;
    if (!NeedSettings(settings, error)) return false;
    std::string payload;
    std::vector<NkObject> objects;
    if (McpTool(settings, "list_todos", "{}", payload, error) && NkParseObjectArray(payload, objects)) {
        for (const auto& obj : objects) todos.push_back(TodoFrom(obj));
        return true;
    }
    const NkHttpResult res = NkRequest(L"GET", Api(settings, L"/todos?isArchived=false&isDeleted=false"), settings.token);
    if (res.status != 200) {
        if (error.empty()) Fail(error, L"Не удалось получить todos", res.status, res.winError);
        return false;
    }
    objects.clear();
    if (!NkParseObjectArray(res.body, objects)) {
        error = L"Сервер вернул непонятный JSON todos.";
        return false;
    }
    for (const auto& obj : objects) todos.push_back(TodoFrom(obj));
    return true;
}

bool NkGetTodo(const std::wstring& id, NkTodo& todo, std::wstring& error) {
    NkSettings settings;
    if (!NeedSettings(settings, error)) return false;
    NkObject obj;
    const NkHttpResult res = NkRequest(L"GET", Api(settings, L"/todos/" + id), settings.token);
    if (res.status == 200 && ParseOne(res.body, obj)) {
        todo = TodoFrom(obj);
        return true;
    }
    std::string payload;
    if (McpTool(settings, "get_todo", IdArgs(id), payload, error) && ParseOne(payload, obj)) {
        todo = TodoFrom(obj);
        return true;
    }
    if (error.empty()) Fail(error, L"Не удалось открыть todo", res.status, res.winError);
    return false;
}

bool NkSaveTodo(NkTodo& todo, std::wstring& error) {
    NkSettings settings;
    if (!NeedSettings(settings, error)) return false;
    NkHttpResult res;
    if (todo.id.empty()) {
        res = NkRequest(L"POST", Api(settings, L"/todos"), settings.token, TodoJson(todo));
    } else {
        res = NkRequest(L"PUT", Api(settings, L"/todos/" + todo.id), settings.token, TodoJson(todo));
    }
    if (res.status >= 200 && res.status < 300) {
        NkObject obj;
        if (ParseOne(res.body, obj)) todo = TodoFrom(obj);
        return true;
    }
    if (todo.id.empty()) {
        std::ostringstream args;
        args << "{\"title\":\"" << NkJsonEscape(todo.title)
             << "\",\"description\":\"" << NkJsonEscape(todo.description) << "\"}";
        std::string payload;
        NkObject obj;
        if (McpTool(settings, "create_todo", args.str(), payload, error) && ParseOne(payload, obj)) {
            todo = TodoFrom(obj);
            return true;
        }
    }
    if (error.empty()) Fail(error, L"Не удалось сохранить todo", res.status, res.winError);
    return false;
}

bool NkDeleteTodo(const std::wstring& id, std::wstring& error) {
    NkSettings settings;
    if (!NeedSettings(settings, error)) return false;
    const NkHttpResult res = NkRequest(L"DELETE", Api(settings, L"/todos/" + id + L"?permanent=true"), settings.token);
    if (res.status != 204 && res.status != 200) {
        Fail(error, L"Не удалось удалить todo", res.status, res.winError);
        return false;
    }
    return true;
}

bool NkToggleTodo(const std::wstring& id, NkTodo& todo, std::wstring& error) {
    NkSettings settings;
    if (!NeedSettings(settings, error)) return false;
    std::string payload;
    NkObject obj;
    if (McpTool(settings, "toggle_todo", IdArgs(id), payload, error) && ParseOne(payload, obj)) {
        todo = TodoFrom(obj);
        return true;
    }
    const NkHttpResult res = NkRequest(L"POST", Api(settings, L"/todos/" + id + L"/toggle-complete"), settings.token);
    if (res.status != 200) {
        if (error.empty()) Fail(error, L"Не удалось переключить todo", res.status, res.winError);
        return false;
    }
    if (!ParseOne(res.body, obj)) {
        error = L"Сервер вернул непонятный JSON todo.";
        return false;
    }
    todo = TodoFrom(obj);
    return true;
}
