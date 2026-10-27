#include "panel.hpp"
#include "guid.hpp"
#include "ui.hpp"
#include "nk_api.hpp"
#include "nk_secure.hpp"
#include "nk_text.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

namespace {

const uint64_t kSilent = 0x1;
const uint64_t kAddDots = 0x8;
const uint64_t kPreserveCase = 0x100;
const uint64_t kAttrHighlight = 0x8000;
const int kGetCurrentItem = 23;

struct OpenPanelInfo {
    size_t StructSize;
    HANDLE hPanel;
    uint64_t Flags;
    const wchar_t* HostFile;
    const wchar_t* CurDir;
    const wchar_t* Format;
    const wchar_t* PanelTitle;
    const void* InfoLines;
    size_t InfoLinesNumber;
    const wchar_t* const* DescrFiles;
    size_t DescrFilesNumber;
    const void* PanelModesArray;
    size_t PanelModesNumber;
    intptr_t StartPanelMode;
    int StartSortMode;
    intptr_t StartSortOrder;
    const void* KeyBar;
    const wchar_t* ShortcutData;
    unsigned long long FreeSize;
    void* UserData;
    void* UserDataFree;
    void* Instance;
};

struct SetDirectoryInfo {
    size_t StructSize;
    HANDLE hPanel;
    const wchar_t* Dir;
    intptr_t Reserved;
    uint64_t OpMode;
    void* UserData;
    void* UserDataFree;
    void* Instance;
};

struct GetFindDataInfo {
    size_t StructSize;
    HANDLE hPanel;
    PluginPanelItem* PanelItem;
    size_t ItemsNumber;
    uint64_t OpMode;
    void* Instance;
};

struct FreeFindDataInfo {
    size_t StructSize;
    HANDLE hPanel;
    PluginPanelItem* PanelItem;
    size_t ItemsNumber;
    void* Instance;
};

struct GetFilesInfo {
    size_t StructSize;
    HANDLE hPanel;
    PluginPanelItem* PanelItem;
    size_t ItemsNumber;
    BOOL Move;
    const wchar_t* DestPath;
    uint64_t OpMode;
    void* Instance;
};

struct PutFilesInfo {
    size_t StructSize;
    HANDLE hPanel;
    PluginPanelItem* PanelItem;
    size_t ItemsNumber;
    BOOL Move;
    const wchar_t* SrcPath;
    uint64_t OpMode;
    void* Instance;
};

struct DeleteFilesInfo {
    size_t StructSize;
    HANDLE hPanel;
    PluginPanelItem* PanelItem;
    size_t ItemsNumber;
    uint64_t OpMode;
    void* Instance;
};

struct ClosePanelInfo {
    size_t StructSize;
    HANDLE hPanel;
    void* Instance;
};

struct PanelMode {
    const wchar_t* ColumnTypes;
    const wchar_t* ColumnWidths;
    const wchar_t* const* ColumnTitles;
    const wchar_t* StatusColumnTypes;
    const wchar_t* StatusColumnWidths;
    uint64_t Flags;
};

struct KeyLabel {
    FarKey Key;
    const wchar_t* Text;
    const wchar_t* LongText;
};

struct KeyTitles {
    size_t CountLabels;
    KeyLabel* Labels;
};

enum class Place { Root, Notes, Todos };

struct Row {
    std::wstring name;
    std::wstring extra;
    std::wstring id;
    const wchar_t* columns[1]{};
};

struct Listing {
    std::vector<Row> rows;
    std::vector<PluginPanelItem> items;
};

std::set<void*> g_live;
std::map<PluginPanelItem*, Listing*> g_listings;

struct NotePanel {
    Place place = Place::Root;
    std::wstring title = L" NoteKeeper ";
    std::wstring format = L"NoteKeeper";
    std::wstring curDir = L"\\";
    std::vector<NkNote> notes;
    std::vector<NkTodo> todos;
    bool loaded = false;

    static bool Live(void* panel) { return g_live.count(panel) != 0; }
    NotePanel() { g_live.insert(this); }
    ~NotePanel() { g_live.erase(this); }

    void GoRoot() {
        place = Place::Root;
        title = L" NoteKeeper ";
        curDir = L"\\";
    }
    void GoNotes() {
        place = Place::Notes;
        title = L" Notes ";
        curDir = L"\\Notes";
    }
    void GoTodos() {
        place = Place::Todos;
        title = L" Todos ";
        curDir = L"\\Todos";
    }

    bool Reload(bool force = false) {
        if (loaded && !force) return true;
        std::wstring error;
        bool ok = true;
        if (place == Place::Notes || place == Place::Root) {
            if (!NkListNotes(notes, error)) {
                if (place == Place::Notes && !error.empty()) Say(error.c_str());
                ok = false;
            }
        }
        if (place == Place::Todos || place == Place::Root) {
            std::wstring todosError;
            if (!NkListTodos(todos, todosError)) {
                if (place == Place::Todos && !todosError.empty()) Say(todosError.c_str());
                if (error.empty()) error = todosError;
                ok = false;
            }
        }
        loaded = true;
        return ok;
    }
};

NotePanel* PanelOf(HANDLE handle) {
    auto* panel = static_cast<NotePanel*>(handle);
    return NotePanel::Live(panel) ? panel : nullptr;
}

std::wstring CurrentName() {
    if (!g_info.PanelControl) return {};
    FarGetPanelItem query{};
    query.StructSize = sizeof(query);
    const intptr_t bytes = g_info.PanelControl(PANEL_ACTIVE, kGetCurrentItem, 0, &query);
    if (bytes <= 0) return {};
    std::vector<uint8_t> buffer(static_cast<size_t>(bytes));
    auto* item = reinterpret_cast<PluginPanelItem*>(buffer.data());
    query.Size = static_cast<size_t>(bytes);
    query.Item = item;
    if (g_info.PanelControl(PANEL_ACTIVE, kGetCurrentItem, 0, &query) == 0 || !item->FileName) return {};
    return item->FileName;
}

void FillItem(PluginPanelItem& item, const std::wstring& name, bool directory, const FILETIME* time = nullptr) {
    item.FileName = name.c_str();
    item.FileAttributes = directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if (time && (time->dwLowDateTime || time->dwHighDateTime)) {
        item.CreationTime = *time;
        item.LastAccessTime = *time;
        item.LastWriteTime = *time;
        item.ChangeTime = *time;
    }
}

Listing* Publish(std::vector<Row> rows, std::vector<PluginPanelItem> items) {
    auto* listing = new Listing{std::move(rows), std::move(items)};
    for (size_t i = 0; i < listing->rows.size() && i < listing->items.size(); ++i) {
        listing->items[i].FileName = listing->rows[i].name.c_str();
        if (!listing->rows[i].extra.empty()) {
            listing->rows[i].columns[0] = listing->rows[i].extra.c_str();
            listing->items[i].CustomColumnData = listing->rows[i].columns;
            listing->items[i].CustomColumnNumber = 1;
        }
    }
    g_listings[listing->items.data()] = listing;
    return listing;
}

void Redraw() {
    if (g_info.PanelControl) {
        g_info.PanelControl(PANEL_ACTIVE, FCTL_UPDATEPANEL, 1, nullptr);
        g_info.PanelControl(PANEL_ACTIVE, FCTL_REDRAWPANEL, 0, nullptr);
    }
}

std::wstring SafeName(std::wstring name) {
    for (auto& ch : name) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' ||
            ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
            ch = L'_';
    }
    return name.empty() ? L"untitled" : name;
}

const NkNote* FindNote(const NotePanel* panel, const std::wstring& name) {
    for (const auto& note : panel->notes)
        if (note.title == name || SafeName(note.title) + L".md" == name) return &note;
    return nullptr;
}

NkNote* FindNote(NotePanel* panel, const std::wstring& name) {
    return const_cast<NkNote*>(FindNote(static_cast<const NotePanel*>(panel), name));
}

const NkTodo* FindTodo(const NotePanel* panel, const std::wstring& name) {
    for (const auto& todo : panel->todos) {
        const std::wstring mark = todo.completed ? L"[x] " : L"[ ] ";
        if (todo.title == name || mark + todo.title == name) return &todo;
    }
    return nullptr;
}

NkTodo* FindTodo(NotePanel* panel, const std::wstring& name) {
    return const_cast<NkTodo*>(FindTodo(static_cast<const NotePanel*>(panel), name));
}

std::wstring JoinLocal(const wchar_t* dir, const wchar_t* name) {
    std::wstring path = dir ? dir : L"";
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path += L'\\';
    path += name;
    return path;
}

struct MacroValue {
    int Type;
    union {
        long long Integer;
        long long Boolean;
        double Double;
        const wchar_t* String;
        const char* Bytes;
        void* Pointer;
        struct { void* Data; size_t Size; } Binary;
        struct { MacroValue* Values; size_t Count; } Array;
    };
};

struct MacroExec {
    size_t StructSize;
    unsigned long long Flags;
    const wchar_t* SequenceText;
    size_t InCount;
    MacroValue* InValues;
    size_t OutCount;
    const MacroValue* OutValues;
};

const wchar_t kMarkdownGuid[] = L"E4B1428A-16D3-4F8C-9B2A-7D5F1C3E9A4B";

bool RunMacro(const wchar_t* code, const wchar_t* pluginId, const wchar_t* path) {
    if (!g_info.MacroControl) return false;
    MacroValue in[2]{};
    in[0].Type = 2;
    in[0].String = pluginId;
    size_t count = 1;
    if (path) {
        in[1].Type = 2;
        in[1].String = path;
        count = 2;
    }
    MacroExec exec{};
    exec.StructSize = sizeof(exec);
    exec.SequenceText = code;
    exec.InCount = count;
    exec.InValues = in;
    if (!g_info.MacroControl(MainGuid, 10, 0, &exec) || !exec.OutCount || !exec.OutValues)
        return false;
    const MacroValue& out = exec.OutValues[0];
    if (out.Type == 4) return out.Boolean != 0;
    if (out.Type == 1) return out.Integer != 0;
    return false;
}

bool OpenMarkdown(const std::wstring& path) {
    if (!RunMacro(L"local id = ...; return Plugin.Exist(id)", kMarkdownGuid, nullptr))
        return false;
    return RunMacro(
        L"local id, path = ...; return mf.postmacro(function(a, b) Plugin.Command(a, b) end, id, path)",
        kMarkdownGuid, path.c_str());
}

void TakeHeading(std::wstring& title, const std::wstring& body) {
    if (body.size() < 2 || body[0] != L'#' || body[1] != L' ') return;
    const auto nl = body.find(L'\n');
    title = nl == std::wstring::npos ? body.substr(2) : body.substr(2, nl - 2);
    while (!title.empty() && (title.back() == L'\r' || title.back() == L' '))
        title.pop_back();
}

bool WriteText(const std::wstring& path, const std::wstring& text) {
    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    const std::string utf8 = NkUtf8(text);
    std::string bytes(reinterpret_cast<const char*>(bom), sizeof(bom));
    bytes += utf8;
    return NkWriteSecureFile(path, bytes.data(), bytes.size());
}

std::wstring ReadText(const std::wstring& path) {
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) return {};
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    std::string raw(size > 0 ? static_cast<size_t>(size) : 0, '\0');
    if (size > 0) fread(raw.data(), 1, raw.size(), file);
    fclose(file);
    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF)
        raw.erase(0, 3);
    return NkWide(raw);
}

std::wstring TempFile(const wchar_t* prefix, const wchar_t* ext) {
    std::wstring error;
    return NkSecureTempPath(L"FarNoteKeeper", prefix, ext, error);
}

bool EditNote(NotePanel* panel, NkNote note) {
    std::wstring error;
    if (!note.id.empty() && !NkGetNote(note.id, note, error)) {
        if (!error.empty()) Say(error.c_str());
        return false;
    }
    const std::wstring path = TempFile(L"nt", L".md");
    if (!WriteText(path, note.content)) {
        Say(L"Не удалось открыть временный файл.");
        return false;
    }
    if (g_info.Editor)
        g_info.Editor(path.c_str(), note.title.c_str(), -1, -1, -1, -1, 0, 1, 1, 65001);
    note.content = ReadText(path);
    TakeHeading(note.title, note.content);
    DeleteFileW(path.c_str());
    if (!NkSaveNote(note, error)) {
        if (!error.empty()) Say(error.c_str());
        return false;
    }
    panel->Reload(true);
    return true;
}

bool EditTodo(NotePanel* panel, NkTodo todo) {
    std::wstring error;
    if (!todo.id.empty() && !NkGetTodo(todo.id, todo, error)) {
        if (!error.empty()) Say(error.c_str());
        return false;
    }
    if (!EditTodoMeta(todo)) return false;
    const std::wstring path = TempFile(L"td", L".md");
    std::wstring body = L"# " + todo.title + L"\n\n" + todo.description;
    if (!WriteText(path, body)) {
        Say(L"Не удалось открыть временный файл.");
        return false;
    }
    if (g_info.Editor)
        g_info.Editor(path.c_str(), todo.title.c_str(), -1, -1, -1, -1, 0, 1, 1, 65001);
    body = ReadText(path);
    DeleteFileW(path.c_str());
    if (body.size() >= 2 && body[0] == L'#' && body[1] == L' ') {
        const auto nl = body.find(L'\n');
        todo.title = nl == std::wstring::npos ? body.substr(2) : body.substr(2, nl - 2);
        while (!todo.title.empty() && (todo.title.back() == L'\r' || todo.title.back() == L' '))
            todo.title.pop_back();
        std::wstring rest = nl == std::wstring::npos ? std::wstring{} : body.substr(nl + 1);
        if (!rest.empty() && rest[0] == L'\n') rest.erase(0, 1);
        if (!rest.empty() && rest[0] == L'\r') rest.erase(0, rest.size() > 1 && rest[1] == L'\n' ? 2 : 1);
        todo.description = rest;
    } else {
        todo.description = body;
    }
    if (!NkSaveTodo(todo, error)) {
        if (!error.empty()) Say(error.c_str());
        return false;
    }
    panel->Reload(true);
    return true;
}

void ViewText(const std::wstring& title, const std::wstring& text) {
    const std::wstring path = TempFile(L"vw", L".md");
    if (path.empty() || !WriteText(path, text)) return;
    if (OpenMarkdown(path)) return;
    if (g_info.Viewer)
        g_info.Viewer(path.c_str(), title.c_str(), -1, -1, -1, -1, 0x200 | 0x8, 65001);
    else
        DeleteFileW(path.c_str());
}

void AddRecord(NotePanel* panel) {
    EatLeftoverHotkeys();
    if (panel->place == Place::Root) {
        const intptr_t pick = g_info.Menu
            ? [&]() {
                FarMenuItem items[2]{};
                items[0].Text = L"Note";
                items[0].Flags = 0x10000;
                items[1].Text = L"Todo";
                return g_info.Menu(MainGuid, MenuGuid, -1, -1, 0, 0x2, L"Что создать?",
                                   L"Enter — выбрать", nullptr, nullptr, nullptr, items, 2);
            }()
            : -1;
        if (pick == 0) panel->GoNotes();
        else if (pick == 1) panel->GoTodos();
        else return;
    }
    if (panel->place == Place::Notes) {
        NkNote note;
        note.title = L"Новая заметка";
        note.content = L"# Новая заметка\n\n";
        note.folder = L"default";
        note.priority = L"medium";
        EditNote(panel, note);
        return;
    }
    if (panel->place == Place::Todos) {
        NkTodo todo;
        todo.title = L"Новый todo";
        todo.priority = L"medium";
        EditTodo(panel, todo);
    }
}

const intptr_t kHotkeyCreate = 1;
const intptr_t kHotkeyEdit = 2;
HANDLE g_hotkeyPanel = nullptr;

void RunNoteHotkey(intptr_t action) {
    auto* panel = PanelOf(g_hotkeyPanel);
    if (!panel) return;
    if (action == kHotkeyCreate || panel->place == Place::Root) {
        AddRecord(panel);
    } else if (panel->place == Place::Notes) {
        auto* note = FindNote(panel, CurrentName());
        if (note) EditNote(panel, *note);
        else AddRecord(panel);
    } else {
        auto* todo = FindTodo(panel, CurrentName());
        if (todo) EditTodo(panel, *todo);
        else AddRecord(panel);
    }
    Redraw();
}

bool PostNoteHotkey(HANDLE hPanel, intptr_t action) {
    g_hotkeyPanel = hPanel;
    if (g_info.AdvControl && g_info.AdvControl(MainGuid, ACTL_SYNCHRO, 0, reinterpret_cast<void*>(action)))
        return true;
    RunNoteHotkey(action);
    return true;
}

}  // namespace

HANDLE OpenNotePanel() {
    NkSettings settings;
    NkLoadSettings(settings);
    if (!NkConfigured(settings)) {
        if (EditSyncSettings(settings.baseUrl, settings.token))
            NkSaveSettings(settings);
        NkLoadSettings(settings);
    }
    auto* panel = new NotePanel();
    if (!NkConfigured(settings)) {
        panel->title = L" NoteKeeper · Alt+F9 — URL и PAT ";
        panel->loaded = true;
        return panel;
    }
    std::wstring notesError;
    std::wstring todosError;
    const bool notesOk = NkListNotes(panel->notes, notesError);
    const bool todosOk = NkListTodos(panel->todos, todosError);
    panel->loaded = true;
    if (!notesOk && !todosOk) {
        const std::wstring& error = notesError.empty() ? todosError : notesError;
        if (!error.empty()) Say(error.c_str());
    }
    return panel;
}

extern "C" {

void WINAPI ClosePanelW(const ClosePanelInfo* info) {
    NkWipeTempPrefix(L"FarNoteKeeper", L"vw");
    if (!info) return;
    delete PanelOf(info->hPanel);
}

void WINAPI GetOpenPanelInfoW(OpenPanelInfo* info) {
    auto* panel = PanelOf(info ? info->hPanel : nullptr);
    if (!info || !panel) return;
    info->StructSize = sizeof(OpenPanelInfo);
    info->hPanel = panel;
    info->Flags = kAddDots | kPreserveCase | kAttrHighlight;
    info->Format = panel->format.c_str();
    info->PanelTitle = panel->title.c_str();
    info->CurDir = panel->curDir.c_str();
    static const wchar_t* rootTitles[] = { L"Имя", L"Описание" };
    static const wchar_t* noteTitles[] = { L"Имя", L"Папка" };
    static const wchar_t* todoTitles[] = { L"Имя", L"Повтор" };
    static const PanelMode rootMode{ L"N,C0", L"0,24", rootTitles, L"N", L"0", 1 };
    static const PanelMode noteMode{ L"N,C0", L"0,20", noteTitles, L"N,C0", L"0,0", 1 };
    static const PanelMode todoMode{ L"N,C0", L"0,20", todoTitles, L"N,C0", L"0,0", 1 };
    static KeyLabel rootKeys[] = {
        {{VK_F3, 0}, L"", L""},
        {{VK_F4, 0}, L"Новый", L"Новая запись"},
        {{VK_F4, SHIFT_PRESSED}, L"Новый", L"Новая запись"},
        {{VK_F7, 0}, L"Новый", L"Новая запись"},
        {{VK_F9, LEFT_ALT_PRESSED}, L"Обнов", L"Обновить"},
    };
    static KeyLabel itemKeys[] = {
        {{VK_F3, 0}, L"Смотр", L"Просмотр"},
        {{VK_F4, 0}, L"Изм", L"Изменить"},
        {{VK_F4, SHIFT_PRESSED}, L"Новый", L"Новая запись"},
        {{VK_F5, 0}, L"Done", L"Переключить todo"},
        {{VK_F7, 0}, L"Новый", L"Новая запись"},
        {{VK_F8, 0}, L"Удал", L"Удалить"},
        {{VK_F9, LEFT_ALT_PRESSED}, L"Обнов", L"Обновить"},
    };
    static KeyTitles rootBar{ std::size(rootKeys), rootKeys };
    static KeyTitles itemBar{ std::size(itemKeys), itemKeys };
    if (panel->place == Place::Root) {
        info->PanelModesArray = &rootMode;
        info->KeyBar = &rootBar;
    } else if (panel->place == Place::Notes) {
        info->PanelModesArray = &noteMode;
        info->KeyBar = &itemBar;
    } else {
        info->PanelModesArray = &todoMode;
        info->KeyBar = &itemBar;
    }
    info->PanelModesNumber = 1;
    info->StartPanelMode = 0;
    info->StartSortMode = 2;
    info->StartSortOrder = 0;
}

intptr_t WINAPI GetFindDataW(GetFindDataInfo* info) {
    auto* panel = PanelOf(info ? info->hPanel : nullptr);
    if (!info || !panel) return 0;
    panel->Reload(false);
    std::vector<Row> rows;
    std::vector<PluginPanelItem> items;
    if (panel->place == Place::Root) {
        Row notes{ L"Notes", L"заметки", {} };
        Row todos{ L"Todos", L"задачи", {} };
        PluginPanelItem a{};
        PluginPanelItem b{};
        FillItem(a, notes.name, true);
        FillItem(b, todos.name, true);
        rows.push_back(std::move(notes));
        rows.push_back(std::move(todos));
        items.push_back(a);
        items.push_back(b);
    } else if (panel->place == Place::Notes) {
        for (const auto& note : panel->notes) {
            Row row;
            row.name = SafeName(note.title) + L".md";
            row.extra = note.folder;
            row.id = note.id;
            PluginPanelItem cell{};
            FILETIME time{};
            NkIsoToFileTime(note.updatedAt, &time);
            FillItem(cell, row.name, false, &time);
            rows.push_back(std::move(row));
            items.push_back(cell);
        }
    } else {
        for (const auto& todo : panel->todos) {
            Row row;
            row.name = (todo.completed ? L"[x] " : L"[ ] ") + todo.title;
            if (!todo.repeat.empty() && NkLower(todo.repeat) != L"none")
                row.extra = todo.repeat;
            if (!todo.reminder.empty()) {
                const std::wstring local = NkIsoToLocal(todo.reminder);
                if (!row.extra.empty()) row.extra += L" ";
                row.extra += local.size() >= 16 ? local.substr(11, 5) : local;
            }
            if (row.extra.empty() && !todo.tags.empty()) {
                row.extra = todo.tags[0];
                for (size_t i = 1; i < todo.tags.size(); ++i) row.extra += L", " + todo.tags[i];
            }
            row.id = todo.id;
            PluginPanelItem cell{};
            FILETIME time{};
            NkIsoToFileTime(todo.updatedAt, &time);
            FillItem(cell, row.name, false, &time);
            rows.push_back(std::move(row));
            items.push_back(cell);
        }
    }
    if (items.empty()) {
        info->PanelItem = nullptr;
        info->ItemsNumber = 0;
        return 1;
    }
    Listing* listing = Publish(std::move(rows), std::move(items));
    info->PanelItem = listing->items.data();
    info->ItemsNumber = listing->items.size();
    return 1;
}

void WINAPI FreeFindDataW(const FreeFindDataInfo* info) {
    if (!info || !info->PanelItem) return;
    auto found = g_listings.find(info->PanelItem);
    if (found == g_listings.end()) return;
    Listing* listing = found->second;
    g_listings.erase(found);
    delete listing;
}

intptr_t WINAPI SetDirectoryW(const SetDirectoryInfo* info) {
    auto* panel = PanelOf(info ? info->hPanel : nullptr);
    if (!info || !panel || !info->Dir) return 0;
    std::wstring dir = info->Dir;
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    if (dir == L".." || dir == L"\\..") {
        if (panel->place == Place::Root) return 0;
        panel->GoRoot();
        return 1;
    }
    const std::wstring name = dir.find_last_of(L"\\/") == std::wstring::npos ? dir : dir.substr(dir.find_last_of(L"\\/") + 1);
    if (panel->place == Place::Root) {
        if (NkLower(name) == L"notes") {
            panel->GoNotes();
            return 1;
        }
        if (NkLower(name) == L"todos") {
            panel->GoTodos();
            return 1;
        }
    }
    return 0;
}

intptr_t WINAPI GetFilesW(GetFilesInfo* info) {
    auto* panel = PanelOf(info ? info->hPanel : nullptr);
    if (!info || !panel || !info->DestPath || panel->place == Place::Root) return 0;
    const bool silent = (info->OpMode & kSilent) != 0;
    for (size_t i = 0; i < info->ItemsNumber; ++i) {
        const wchar_t* name = info->PanelItem[i].FileName;
        if (!name || wcscmp(name, L"..") == 0) continue;
        std::wstring error;
        if (panel->place == Place::Notes) {
            const auto* note = FindNote(panel, name);
            if (!note) continue;
            NkNote full = *note;
            if (!NkGetNote(note->id, full, error)) {
                if (!silent && !error.empty()) Say(error.c_str());
                return 0;
            }
            if (!WriteText(JoinLocal(info->DestPath, SafeName(full.title).c_str()) + L".md", full.content)) {
                if (!silent) Say(L"Не удалось записать файл.");
                return 0;
            }
        } else {
            const auto* todo = FindTodo(panel, name);
            if (!todo) continue;
            NkTodo full = *todo;
            if (!NkGetTodo(todo->id, full, error)) {
                if (!silent && !error.empty()) Say(error.c_str());
                return 0;
            }
            const std::wstring text = L"# " + full.title + L"\n\n" + full.description;
            if (!WriteText(JoinLocal(info->DestPath, SafeName(full.title).c_str()) + L".md", text)) {
                if (!silent) Say(L"Не удалось записать файл.");
                return 0;
            }
        }
    }
    return 1;
}

intptr_t WINAPI PutFilesW(PutFilesInfo* info) {
    auto* panel = PanelOf(info ? info->hPanel : nullptr);
    if (!info || !panel || panel->place == Place::Root) return 0;
    for (size_t i = 0; i < info->ItemsNumber; ++i) {
        const wchar_t* name = info->PanelItem[i].FileName;
        if (!name || wcscmp(name, L"..") == 0) continue;
        const std::wstring path = JoinLocal(info->SrcPath, name);
        const std::wstring text = ReadText(path);
        std::wstring error;
        if (panel->place == Place::Notes) {
            NkNote note;
            note.title = SafeName(name);
            const auto dot = note.title.rfind(L'.');
            if (dot != std::wstring::npos) note.title.resize(dot);
            note.content = text;
            note.folder = L"default";
            if (!NkSaveNote(note, error)) {
                if (!error.empty()) Say(error.c_str());
                return 0;
            }
        } else {
            NkTodo todo;
            todo.title = SafeName(name);
            const auto dot = todo.title.rfind(L'.');
            if (dot != std::wstring::npos) todo.title.resize(dot);
            todo.description = text;
            if (!NkSaveTodo(todo, error)) {
                if (!error.empty()) Say(error.c_str());
                return 0;
            }
        }
    }
    panel->Reload(true);
    return 1;
}

intptr_t WINAPI DeleteFilesW(const DeleteFilesInfo* info) {
    auto* panel = PanelOf(info ? info->hPanel : nullptr);
    if (!info || !panel || panel->place == Place::Root) return 0;
    if ((info->OpMode & kSilent) == 0 && !AskYes(L"Удалить выбранные записи на сервере?")) return 0;
    bool changed = false;
    for (size_t i = 0; i < info->ItemsNumber; ++i) {
        const wchar_t* name = info->PanelItem[i].FileName;
        if (!name || wcscmp(name, L"..") == 0) continue;
        std::wstring error;
        if (panel->place == Place::Notes) {
            const auto* note = FindNote(panel, name);
            if (!note) continue;
            if (NkDeleteNote(note->id, error)) changed = true;
            else if (!error.empty()) Say(error.c_str());
        } else {
            const auto* todo = FindTodo(panel, name);
            if (!todo) continue;
            if (NkDeleteTodo(todo->id, error)) changed = true;
            else if (!error.empty()) Say(error.c_str());
        }
    }
    if (changed) panel->Reload(true);
    return changed ? 1 : 0;
}

intptr_t WINAPI ProcessSynchroEventW(const ProcessSynchroEventInfo* info) {
    if (!info || info->Event != SE_COMMONSYNCHRO) return 0;
    RunNoteHotkey(reinterpret_cast<intptr_t>(info->Param));
    return 0;
}

intptr_t WINAPI ProcessPanelInputW(const ProcessPanelInputInfo* info) {
    if (!info || (info->Rec.EventType & 0x7FFF) != KEY_EVENT || !info->Rec.Event.KeyEvent.bKeyDown) return 0;
    auto* panel = PanelOf(info->hPanel);
    if (!panel) return 0;
    const KEY_EVENT_RECORD& key = info->Rec.Event.KeyEvent;
    const bool macro = (info->Rec.EventType & 0x8000) != 0;
    const bool shift = (key.dwControlKeyState & SHIFT_PRESSED) != 0 ||
                       (macro && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
    const bool ctrl = (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    const bool alt = (key.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;

    if ((key.wVirtualKeyCode == VK_F7 || (key.wVirtualKeyCode == VK_F4 && shift)) && !ctrl && !alt) {
        PostNoteHotkey(info->hPanel, kHotkeyCreate);
        return 1;
    }
    if (key.wVirtualKeyCode == VK_F4 && !shift && !ctrl && !alt) {
        PostNoteHotkey(info->hPanel, kHotkeyEdit);
        return 1;
    }

    if (key.wVirtualKeyCode == VK_F9 && alt && !shift && !ctrl) {
        NkSettings settings;
        NkLoadSettings(settings);
        if (!NkConfigured(settings)) {
            if (EditSyncSettings(settings.baseUrl, settings.token)) {
                NkSaveSettings(settings);
                panel->title = L" NoteKeeper ";
            }
        }
        panel->Reload(true);
        Redraw();
        return 1;
    }
    if (key.wVirtualKeyCode == VK_F3 && !shift && !ctrl && !alt && panel->place != Place::Root) {
        std::wstring error;
        if (panel->place == Place::Notes) {
            const auto* note = FindNote(panel, CurrentName());
            if (note) {
                NkNote full = *note;
                if (NkGetNote(note->id, full, error)) ViewText(full.title, full.content);
                else if (!error.empty()) Say(error.c_str());
            }
        } else {
            const auto* todo = FindTodo(panel, CurrentName());
            if (todo) {
                NkTodo full = *todo;
                if (NkGetTodo(todo->id, full, error))
                    ViewText(full.title, L"# " + full.title + L"\n\n" + full.description);
                else if (!error.empty()) Say(error.c_str());
            }
        }
        return 1;
    }
    if (key.wVirtualKeyCode == VK_F5 && !shift && !ctrl && !alt && panel->place == Place::Todos) {
        auto* todo = FindTodo(panel, CurrentName());
        if (todo) {
            std::wstring error;
            NkTodo updated;
            if (!NkToggleTodo(todo->id, updated, error) && !error.empty()) Say(error.c_str());
            panel->Reload(true);
            Redraw();
        }
        return 1;
    }
    if (key.wVirtualKeyCode == VK_SPACE && !shift && !ctrl && !alt && panel->place == Place::Todos) {
        auto* todo = FindTodo(panel, CurrentName());
        if (todo) {
            std::wstring error;
            NkTodo updated;
            if (!NkToggleTodo(todo->id, updated, error) && !error.empty()) Say(error.c_str());
            panel->Reload(true);
            Redraw();
        }
        return 1;
    }
    return 0;
}

}
