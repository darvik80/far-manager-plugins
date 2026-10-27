#include "panel.hpp"
#include "clipboard.hpp"
#include "guid.hpp"
#include "keys.hpp"
#include "text.hpp"
#include "ui.hpp"
#include "sync.hpp"
#include "vault.hpp"

#include <algorithm>
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

enum class Place { Root, Passwords, Keys };

struct Row {
    std::wstring name;
    std::wstring extra;
    const wchar_t* columns[1]{};
};

struct Listing {
    std::vector<Row> rows;
    std::vector<PluginPanelItem> items;
};

std::set<void*> g_live;
std::map<PluginPanelItem*, Listing*> g_listings;

struct VaultPanel {
    Place place = Place::Root;
    std::wstring title = L" Secret Vault ";
    std::wstring format = L"Vault";
    std::wstring curDir = L"\\";
    VaultData data;

    static bool Live(void* panel) { return g_live.count(panel) != 0; }
    VaultPanel() { g_live.insert(this); }
    ~VaultPanel() { g_live.erase(this); }

    bool Reload() {
        std::wstring error;
        if (!LoadVault(data, error)) {
            if (!error.empty()) Say(error.c_str());
            return false;
        }
        return true;
    }

    bool PushRemote() {
        SyncSettings settings;
        LoadSyncSettings(settings);
        if (!SyncConfigured(settings)) return true;
        std::wstring error;
        if (PushVault(data, settings, false, error)) return true;
        if (error.find(L"Конфликт") != std::wstring::npos && AskYes(error.c_str())) {
            error.clear();
            if (PushVault(data, settings, true, error)) return true;
        }
        if (!error.empty()) Say(error.c_str());
        return false;
    }

    bool PullRemote() {
        SyncSettings settings;
        LoadSyncSettings(settings);
        if (!SyncConfigured(settings)) return true;
        const bool first = settings.lastSync.empty();
        const bool hasLocal = !data.passwords.empty() || !data.keys.empty();
        if (first && hasLocal) return true;
        std::wstring error;
        if (!PullVault(data, settings, error)) {
            if (!error.empty()) Say(error.c_str());
            return false;
        }
        return SaveVault(data, error);
    }

    bool Persist() {
        std::wstring error;
        if (!SaveVault(data, error)) {
            if (!error.empty()) Say(error.c_str());
            return false;
        }
        return PushRemote();
    }

    void GoRoot() {
        place = Place::Root;
        title = L" Secret Vault ";
        curDir = L"\\";
    }

    void GoPasswords() {
        place = Place::Passwords;
        title = L" Пароли ";
        curDir = L"\\Пароли";
    }

    void GoKeys() {
        place = Place::Keys;
        title = L" Ключи ";
        curDir = L"\\Ключи";
    }
};

VaultPanel* PanelOf(HANDLE handle) {
    auto* panel = static_cast<VaultPanel*>(handle);
    return VaultPanel::Live(panel) ? panel : nullptr;
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

void FillItem(PluginPanelItem& item, const std::wstring& name, bool directory) {
    item.FileName = name.c_str();
    item.FileAttributes = directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
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

bool AddPassword(VaultPanel* panel) {
    PasswordRecord item;
    item.id = NewRecordId();
    if (!EditPassword(item, true)) return false;
    if (NameTaken(panel->data, false, item.name, item.id)) {
        Say(L"Такое имя уже есть.");
        return false;
    }
    panel->data.passwords.push_back(std::move(item));
    return panel->Persist();
}

bool EditPasswordAt(VaultPanel* panel, const std::wstring& name) {
    auto* item = FindPassword(panel->data, name);
    if (!item) {
        Say(L"Пароль не найден.");
        return false;
    }
    PasswordRecord edited = *item;
    if (!EditPassword(edited, false)) return false;
    if (NameTaken(panel->data, false, edited.name, edited.id)) {
        Say(L"Такое имя уже есть.");
        return false;
    }
    *item = std::move(edited);
    return panel->Persist();
}

bool EqualCi(const std::wstring& a, const wchar_t* b) {
    return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, a.c_str(), -1, b, -1) == CSTR_EQUAL;
}

bool IsPasswordsName(const std::wstring& name) {
    return EqualCi(name, L"Passwords") || EqualCi(name, L"Пароли");
}

bool IsKeysName(const std::wstring& name) {
    return EqualCi(name, L"Keys") || EqualCi(name, L"Ключи");
}

bool IsHintName(const std::wstring& name) {
    return !name.empty() && name.front() == L'(';
}

bool EndsWithCi(const std::wstring& text, const wchar_t* suffix) {
    const size_t n = wcslen(suffix);
    if (text.size() < n) return false;
    return EqualCi(text.substr(text.size() - n), suffix);
}

std::wstring JoinLocal(const wchar_t* dir, const wchar_t* name) {
    if (name && name[0] && ((name[1] == L':') || (name[0] == L'\\' && name[1] == L'\\')))
        return name;
    std::wstring path = dir ? dir : L"";
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path += L'\\';
    if (name) path += name;
    return path;
}

std::wstring FileStem(const std::wstring& path) {
    auto slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    if (EndsWithCi(name, L".pub")) name.resize(name.size() - 4);
    return name.empty() ? L"key" : name;
}

std::wstring UniqueKeyName(const VaultData& data, std::wstring name, const std::wstring& exceptId) {
    if (name.empty()) name = L"key";
    if (!NameTaken(data, true, name, exceptId)) return name;
    for (int i = 2; i < 1000; ++i) {
        const std::wstring candidate = name + L"-" + std::to_wstring(i);
        if (!NameTaken(data, true, candidate, exceptId)) return candidate;
    }
    return name + L"-" + NewRecordId();
}

std::wstring SafeFileName(std::wstring name) {
    for (auto& ch : name) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
            ch = L'_';
    }
    return name.empty() ? L"key" : name;
}

bool WriteText(const std::wstring& path, const std::wstring& text) {
    FILE* file = _wfopen(path.c_str(), L"wb");
    if (!file) return false;
    const std::string bytes = Utf8(text);
    const bool ok = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    fclose(file);
    return ok;
}

void CollectFiles(const std::wstring& path, bool directory, std::vector<std::wstring>& files) {
    if (!directory) {
        files.push_back(path);
        return;
    }
    WIN32_FIND_DATAW found{};
    const std::wstring spec = path + L"\\*";
    HANDLE search = FindFirstFileW(spec.c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0) continue;
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        files.push_back(path + L"\\" + found.cFileName);
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

bool ImportKeyPath(VaultPanel* panel, const std::wstring& path, bool silent, std::wstring& error) {
    if (EndsWithCi(path, L".pub")) return false;
    if (!IsOpenSshPrivateFile(path)) {
        error = L"Нужен OpenSSH-ключ, не .ppk: " + FileStem(path);
        return false;
    }
    KeyRecord item;
    item.id = NewRecordId();
    if (!ImportOpenSshKey(path, {}, item, error)) {
        if (silent) return false;
        const std::wstring pass = AskText(L"Пароль ключа", FileStem(path).c_str(), true);
        if (pass.empty()) return false;
        error.clear();
        if (!ImportOpenSshKey(path, pass, item, error)) return false;
    }
    item.name = UniqueKeyName(panel->data, FileStem(path), item.id);
    panel->data.keys.push_back(std::move(item));
    return true;
}

bool AddKey(VaultPanel* panel) {
    std::wstring type;
    KeyRecord item;
    item.id = NewRecordId();
    if (!CreateKey(item, type)) return false;
    std::wstring error;
    if (type == L"import") {
        const std::wstring path = AskFile(L"Импорт ключа", false);
        if (path.empty()) return false;
        const std::wstring pass = AskText(L"Пароль ключа", L"Если ключ открыт, оставь пустым", true);
        const std::wstring savedName = item.name;
        const std::wstring savedComment = item.comment;
        if (!ImportOpenSshKey(path, pass, item, error)) {
            if (!error.empty()) Say(error.c_str());
            return false;
        }
        item.name = savedName;
        if (!savedComment.empty()) item.comment = savedComment;
    } else if (!GenerateKey(type, item.comment, item, error)) {
        if (!error.empty()) Say(error.c_str());
        return false;
    }
    if (NameTaken(panel->data, true, item.name, item.id)) {
        Say(L"Такое имя уже есть.");
        return false;
    }
    panel->data.keys.push_back(std::move(item));
    return panel->Persist();
}

bool AddRecord(VaultPanel* panel) {
    if (panel->place == Place::Passwords) return AddPassword(panel);
    if (panel->place == Place::Keys) return AddKey(panel);
    const std::wstring name = CurrentName();
    if (IsKeysName(name)) {
        panel->GoKeys();
        AddKey(panel);
        return true;
    }
    if (IsPasswordsName(name)) {
        panel->GoPasswords();
        AddPassword(panel);
        return true;
    }
    const intptr_t pick = Pick(L"Что создать?", {L"Пароль", L"Ключ OpenSSH"});
    if (pick == 0) {
        panel->GoPasswords();
        AddPassword(panel);
    } else if (pick == 1) {
        panel->GoKeys();
        AddKey(panel);
    }
    return true;
}

bool EditKeyAt(VaultPanel* panel, const std::wstring& name) {
    auto* item = FindKey(panel->data, name);
    if (!item) {
        Say(L"Ключ не найден.");
        return false;
    }
    KeyRecord edited = *item;
    if (!EditKeyMeta(edited, false)) return false;
    if (NameTaken(panel->data, true, edited.name, edited.id)) {
        Say(L"Такое имя уже есть.");
        return false;
    }
    item->name = edited.name;
    item->comment = edited.comment;
    return panel->Persist();
}

bool CopyCurrent(VaultPanel* panel) {
    const std::wstring name = CurrentName();
    if (name.empty() || name == L".." || IsHintName(name)) return true;
    std::wstring error;
    if (panel->place == Place::Passwords) {
        const auto* item = FindPassword(panel->data, name);
        if (!item) return true;
        if (!CopySecret(item->secret, error) && !error.empty()) Say(error.c_str());
        return true;
    }
    if (panel->place == Place::Keys) {
        const auto* item = FindKey(panel->data, name);
        if (!item) return true;
        if (!CopySecret(item->privateKey, error) && !error.empty()) Say(error.c_str());
        return true;
    }
    return true;
}

void ShowInfo(VaultPanel* panel) {
    const std::wstring name = CurrentName();
    if (name.empty() || name == L".." || IsHintName(name)) return;
    if (panel->place == Place::Passwords) {
        const auto* item = FindPassword(panel->data, name);
        if (!item) return;
        const std::wstring login = L"Логин: " + item->user;
        const std::wstring host = L"Хост:  " + item->host;
        const std::wstring note = item->note.empty() ? std::wstring() : L"Заметка: " + item->note;
        const wchar_t* lines[6] = { L"Пароль", L"", login.c_str(), host.c_str(), nullptr, nullptr };
        size_t count = 4;
        if (!note.empty()) {
            lines[4] = L"";
            lines[5] = note.c_str();
            count = 6;
        }
        g_info.Message(MainGuid, MenuGuid, FMSG_LEFTALIGN | FMSG_MB_OK, nullptr, lines, count, 1);
        return;
    }
    const auto* item = FindKey(panel->data, name);
    if (!item) return;
    const std::wstring type = L"Тип: " + item->type;
    const std::wstring comment = item->comment.empty() ? std::wstring() : L"Коммент: " + item->comment;
    const wchar_t* lines[6] = { L"Ключ", L"", type.c_str(), item->publicKey.c_str(), nullptr, nullptr };
    size_t count = 4;
    if (!comment.empty()) {
        lines[3] = comment.c_str();
        lines[4] = L"";
        lines[5] = item->publicKey.c_str();
        count = 6;
    }
    g_info.Message(MainGuid, MenuGuid, FMSG_LEFTALIGN | FMSG_MB_OK, nullptr, lines, count, 1);
}

void ExportCurrent(VaultPanel* panel) {
    const std::wstring path = AskFile(L"Экспорт хранилища", true);
    if (path.empty()) return;
    const std::wstring pass = AskText(L"Пароль экспорта", L"Этим паролем откроют файл на другом компьютере", true);
    const std::wstring again = AskText(L"Ещё раз", L"Повтори пароль экспорта", true);
    if (pass.empty() || pass != again) {
        Say(L"Пароли не совпали.");
        return;
    }
    std::wstring error;
    if (!ExportVault(panel->data, path, pass, error) && !error.empty()) Say(error.c_str());
}

const intptr_t kHotkeyCreate = 1;
const intptr_t kHotkeyEdit = 2;
HANDLE g_hotkeyPanel = nullptr;

void RunVaultHotkey(intptr_t action) {
    auto* panel = PanelOf(g_hotkeyPanel);
    if (!panel) return;
    if (action == kHotkeyCreate) {
        AddRecord(panel);
    } else if (panel->place == Place::Root) {
        AddRecord(panel);
    } else {
        const std::wstring name = CurrentName();
        if (name.empty() || name == L".." || IsHintName(name)) AddRecord(panel);
        else if (panel->place == Place::Passwords) EditPasswordAt(panel, name);
        else EditKeyAt(panel, name);
    }
    Redraw();
}

bool PostVaultHotkey(HANDLE hPanel, intptr_t action) {
    g_hotkeyPanel = hPanel;
    if (g_info.AdvControl && g_info.AdvControl(MainGuid, ACTL_SYNCHRO, 0, reinterpret_cast<void*>(action)))
        return true;
    RunVaultHotkey(action);
    return true;
}

void ImportCurrent(VaultPanel* panel) {
    const std::wstring path = AskFile(L"Импорт хранилища", false);
    if (path.empty()) return;
    const std::wstring pass = AskText(L"Пароль файла", L"Если это локальный DPAPI-файл, оставь пустым", true);
    std::wstring error;
    if (!ImportVaultFile(panel->data, path, pass, error)) {
        if (!error.empty()) Say(error.c_str());
        return;
    }
    panel->Persist();
    Redraw();
}

}  // namespace

HANDLE OpenVaultPanel() {
    auto* panel = new VaultPanel();
    if (!panel->Reload()) {
        delete panel;
        return nullptr;
    }
    panel->PullRemote();
    return panel;
}

extern "C" {

void WINAPI ClosePanelW(const ClosePanelInfo* info) {
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
    static const wchar_t* passTitles[] = { L"Имя", L"Логин" };
    static const wchar_t* keyTitles[] = { L"Имя", L"Тип" };
    static const PanelMode rootMode{ L"N,C0", L"0,40", rootTitles, L"N", L"0", 1 };
    static const PanelMode passMode{ L"N,C0", L"0,30", passTitles, L"N,C0", L"0,0", 1 };
    static const PanelMode keyMode{ L"N,C0", L"0,12", keyTitles, L"N,C0", L"0,0", 1 };
    static KeyLabel rootKeys[] = {
        {{VK_F3, 0}, L"", L""},
        {{VK_F4, 0}, L"Новый", L"Новая запись"},
        {{VK_F4, SHIFT_PRESSED}, L"Новый", L"Новая запись"},
        {{VK_F5, LEFT_ALT_PRESSED}, L"Эксп", L"Экспорт"},
        {{VK_F6, LEFT_ALT_PRESSED}, L"Имп", L"Импорт"},
        {{VK_F9, SHIFT_PRESSED | LEFT_ALT_PRESSED}, L"Синк", L"NoteKeeper"},
        {{VK_F5, 0}, L"", L""},
        {{VK_F7, 0}, L"Новый", L"Новая запись"},
        {{VK_F8, 0}, L"", L""},
    };
    static KeyLabel itemKeys[] = {
        {{VK_F3, 0}, L"Инфо", L"Показать без секрета"},
        {{VK_F4, 0}, L"Изм", L"Изменить"},
        {{VK_F5, 0}, L"Копир", L"В буфер"},
        {{VK_F4, SHIFT_PRESSED}, L"Новый", L"Новая запись"},
        {{VK_F7, 0}, L"Новый", L"Новая запись"},
        {{VK_F8, 0}, L"Удал", L"Удалить"},
        {{VK_F5, LEFT_ALT_PRESSED}, L"Эксп", L"Экспорт"},
        {{VK_F6, LEFT_ALT_PRESSED}, L"Имп", L"Импорт"},
        {{VK_F9, SHIFT_PRESSED | LEFT_ALT_PRESSED}, L"Синк", L"NoteKeeper"},
    };
    static KeyTitles rootBar{ std::size(rootKeys), rootKeys };
    static KeyTitles itemBar{ std::size(itemKeys), itemKeys };
    if (panel->place == Place::Root) {
        info->PanelModesArray = &rootMode;
        info->KeyBar = &rootBar;
    } else if (panel->place == Place::Passwords) {
        info->PanelModesArray = &passMode;
        info->KeyBar = &itemBar;
    } else {
        info->PanelModesArray = &keyMode;
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
    std::vector<Row> rows;
    std::vector<PluginPanelItem> items;
    if (panel->place == Place::Root) {
        Row passwords{ L"Пароли", L"логины и пароли" };
        Row keys{ L"Ключи", L"OpenSSH" };
        PluginPanelItem a{};
        PluginPanelItem b{};
        FillItem(a, passwords.name, true);
        FillItem(b, keys.name, true);
        rows.push_back(std::move(passwords));
        rows.push_back(std::move(keys));
        items.push_back(a);
        items.push_back(b);
    } else if (panel->place == Place::Passwords) {
        for (const auto& item : panel->data.passwords) {
            Row row;
            row.name = item.name;
            if (!item.user.empty() && !item.host.empty()) row.extra = item.user + L" @ " + item.host;
            else row.extra = item.user.empty() ? item.host : item.user;
            PluginPanelItem cell{};
            FillItem(cell, row.name, false);
            rows.push_back(std::move(row));
            items.push_back(cell);
        }
    } else {
        for (const auto& item : panel->data.keys) {
            Row row;
            row.name = item.name;
            row.extra = item.type;
            PluginPanelItem cell{};
            FillItem(cell, row.name, false);
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
        if (IsPasswordsName(name)) {
            panel->GoPasswords();
            return 1;
        }
        if (IsKeysName(name)) {
            panel->GoKeys();
            return 1;
        }
    }
    return 0;
}

intptr_t WINAPI PutFilesW(PutFilesInfo* info) {
    auto* panel = PanelOf(info ? info->hPanel : nullptr);
    if (!info || !panel) return 0;
    const bool silent = (info->OpMode & kSilent) != 0;
    std::vector<std::wstring> files;
    for (size_t i = 0; i < info->ItemsNumber; ++i) {
        const wchar_t* name = info->PanelItem[i].FileName;
        if (!name || wcscmp(name, L"..") == 0) continue;
        const bool directory = (info->PanelItem[i].FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        CollectFiles(JoinLocal(info->SrcPath, name), directory, files);
    }
    int imported = 0;
    std::wstring lastError;
    for (const auto& path : files) {
        if (EndsWithCi(path, L".pub")) continue;
        std::wstring error;
        if (ImportKeyPath(panel, path, silent, error)) ++imported;
        else if (!error.empty()) lastError = error;
    }
    if (imported == 0) {
        if (!silent)
            Say(lastError.empty() ? L"Нет OpenSSH-ключей для копирования. Нужен приватный файл, не .pub." : lastError.c_str());
        return 0;
    }
    if (!panel->Persist()) return 0;
    panel->GoKeys();
    return 1;
}

intptr_t WINAPI GetFilesW(GetFilesInfo* info) {
    auto* panel = PanelOf(info ? info->hPanel : nullptr);
    if (!info || !panel || !info->DestPath || panel->place != Place::Keys) return 0;
    const bool silent = (info->OpMode & kSilent) != 0;
    for (size_t i = 0; i < info->ItemsNumber; ++i) {
        const wchar_t* name = info->PanelItem[i].FileName;
        if (!name || wcscmp(name, L"..") == 0 || IsHintName(name)) continue;
        const auto* item = FindKey(panel->data, name);
        if (!item) continue;
        const std::wstring file = SafeFileName(item->name);
        const std::wstring privatePath = JoinLocal(info->DestPath, file.c_str());
        if (!WriteText(privatePath, item->privateKey)) {
            if (!silent) Say(L"Не удалось записать ключ.");
            return 0;
        }
        if (!item->publicKey.empty()) {
            const std::wstring pubName = file + L".pub";
            WriteText(JoinLocal(info->DestPath, pubName.c_str()), item->publicKey);
        }
    }
    return 1;
}

intptr_t WINAPI DeleteFilesW(const DeleteFilesInfo* info) {
    auto* panel = PanelOf(info ? info->hPanel : nullptr);
    if (!info || !panel || panel->place == Place::Root) return 0;
    if ((info->OpMode & kSilent) == 0 && !AskYes(L"Удалить выбранные записи?")) return 0;
    bool changed = false;
    for (size_t i = 0; i < info->ItemsNumber; ++i) {
        const wchar_t* name = info->PanelItem[i].FileName;
        if (!name || wcscmp(name, L"..") == 0 || IsHintName(name)) continue;
        if (panel->place == Place::Passwords) {
            const auto before = panel->data.passwords.size();
            panel->data.passwords.erase(std::remove_if(panel->data.passwords.begin(), panel->data.passwords.end(),
                                                       [&](const PasswordRecord& item) { return Lower(item.name) == Lower(name); }),
                                        panel->data.passwords.end());
            changed = changed || panel->data.passwords.size() != before;
        } else {
            const auto before = panel->data.keys.size();
            panel->data.keys.erase(std::remove_if(panel->data.keys.begin(), panel->data.keys.end(),
                                                  [&](const KeyRecord& item) { return Lower(item.name) == Lower(name); }),
                                   panel->data.keys.end());
            changed = changed || panel->data.keys.size() != before;
        }
    }
    if (changed) panel->Persist();
    return changed ? 1 : 0;
}

intptr_t WINAPI ProcessSynchroEventW(const ProcessSynchroEventInfo* info) {
    if (!info || info->Event != SE_COMMONSYNCHRO) return 0;
    RunVaultHotkey(reinterpret_cast<intptr_t>(info->Param));
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
        PostVaultHotkey(info->hPanel, kHotkeyCreate);
        return 1;
    }
    if (key.wVirtualKeyCode == VK_F4 && !shift && !ctrl && !alt) {
        PostVaultHotkey(info->hPanel, kHotkeyEdit);
        return 1;
    }

    if (key.wVirtualKeyCode == VK_F5 && alt && !shift && !ctrl) {
        ExportCurrent(panel);
        return 1;
    }
    if (key.wVirtualKeyCode == VK_F6 && alt && !shift && !ctrl) {
        ImportCurrent(panel);
        return 1;
    }
    if (key.wVirtualKeyCode == VK_F9 && alt && shift && !ctrl) {
        std::wstring error;
        SyncSettings settings;
        LoadSyncSettings(settings);
        if (!SyncConfigured(settings)) {
            Say(L"Сначала укажи URL и PAT в настройках плагина.");
            return 1;
        }
        if (!SyncVault(panel->data, settings, false, error)) {
            if (error.find(L"Конфликт") != std::wstring::npos && AskYes(error.c_str())) {
                error.clear();
                if (!SyncVault(panel->data, settings, true, error) && !error.empty()) Say(error.c_str());
            } else if (!error.empty()) {
                Say(error.c_str());
            }
        }
        panel->Persist();
        Redraw();
        return 1;
    }
    if (panel->place == Place::Root) return 0;
    if ((key.wVirtualKeyCode == VK_F5 && !shift && !ctrl && !alt) ||
        (key.wVirtualKeyCode == 'C' && ctrl && !alt && !shift)) {
        CopyCurrent(panel);
        return 1;
    }
    if (key.wVirtualKeyCode == VK_F3 && !shift && !ctrl && !alt) {
        ShowInfo(panel);
        return 1;
    }
    return 0;
}

}
