#include "panel.hpp"
#include "guid.hpp"
#include "session_store.hpp"
#include "sftp_client.hpp"
#include "ssh_config.hpp"
#include "teleport.hpp"
#include "ui.hpp"
#include "vault_link.hpp"
#include "far/form.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
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

struct MakeDirectoryInfo {
    size_t StructSize;
    HANDLE hPanel;
    const wchar_t* Name;
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

struct Row {
    std::wstring name;
    std::wstring host;
    std::wstring key;
    const wchar_t* columns[2]{};
};

struct Listing {
    std::vector<Row> rows;
    std::vector<PluginPanelItem> items;
};

std::set<void*> g_live;
std::map<PluginPanelItem*, Listing*> g_listings;

std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int chars = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(chars > 0 ? chars : 0, L'\0');
    if (chars > 0)
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), chars);
    return out;
}

std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(bytes > 0 ? bytes : 0, '\0');
    if (bytes > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

std::wstring Lower(std::wstring text) {
    for (auto& ch : text) ch = towlower(ch);
    return text;
}

void UnixToFileTime(unsigned long seconds, FILETIME* fileTime) {
    ULARGE_INTEGER value;
    value.QuadPart = (static_cast<unsigned long long>(seconds) + 11644473600ULL) * 10000000ULL;
    fileTime->dwLowDateTime = value.LowPart;
    fileTime->dwHighDateTime = value.HighPart;
}

std::wstring JoinLocal(const std::wstring& dir, const wchar_t* name) {
    std::wstring path = dir;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path += L'\\';
    if (name) path += name;
    return path;
}

std::string JoinRemote(const std::string& dir, const std::string& name) {
    if (name.empty()) return dir.empty() ? "/" : dir;
    if (!name.empty() && name[0] == '/') return name;
    if (dir.empty() || dir == "/") return "/" + name;
    if (dir.back() == '/') return dir + name;
    return dir + "/" + name;
}

std::string ParentRemote(std::string dir) {
    if (dir.empty() || dir == "/") return "/";
    if (dir.back() == '/') dir.pop_back();
    const auto slash = dir.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return "/";
    return dir.substr(0, slash);
}

struct SshPanel {
    bool sites = true;
    std::string remote = "/";
    std::wstring title = L" SSH ";
    std::wstring format = L"SSH";
    std::wstring curDir = L"\\";
    SftpConnection connection;
    std::vector<StoredServer> cached;
    StoredServer current;

    static bool Live(void* panel) { return g_live.count(panel) != 0; }

    SshPanel() { g_live.insert(this); }
    ~SshPanel() {
        g_live.erase(this);
        connection.Close();
    }

    void ShowSites() {
        sites = true;
        connection.Close();
        remote.clear();
        title = L" SSH ";
        format = L"SSH";
        curDir = L"\\";
        current = {};
        cached = CombinedSites();
    }

    void OpenSite(const StoredServer& site) {
        sites = false;
        current = site;
        remote = connection.Home();
        const std::wstring who = site.user.empty() ? site.host : site.user + L"@" + site.host;
        title = L" " + who + L" ";
        format = L"SFTP";
        curDir = Wide(remote);
    }

    const StoredServer* Find(const std::wstring& name) const {
        const std::wstring key = Lower(name);
        for (const auto& site : cached)
            if (Lower(site.name) == key) return &site;
        return nullptr;
    }
};

SshPanel* PanelOf(HANDLE handle) {
    auto* panel = static_cast<SshPanel*>(handle);
    return SshPanel::Live(panel) ? panel : nullptr;
}

struct ServerForm {
    wchar_t* name;
    wchar_t* host;
    wchar_t* port;
    wchar_t* user;
    wchar_t* key;
    wchar_t* proxy;
    wchar_t* password;
};

void PullServerForm(HANDLE dialog, ServerForm* form) {
    if (!form || !g_info.SendDlgMessage) return;
    farform::ReadEdit(g_info, dialog, 2, form->name, 128);
    farform::ReadEdit(g_info, dialog, 4, form->host, 256);
    farform::ReadEdit(g_info, dialog, 6, form->port, 16);
    farform::ReadEdit(g_info, dialog, 8, form->user, 128);
    farform::ReadEdit(g_info, dialog, 10, form->key, 520);
    farform::ReadEdit(g_info, dialog, 12, form->proxy, 256);
    farform::ReadEdit(g_info, dialog, 14, form->password, 256);
}

intptr_t WINAPI ServerDlgProc(HANDLE dialog, intptr_t msg, intptr_t param1, void* param2) {
    if ((msg == DN_BTNCLICK && param1 == 16) || (msg == DN_CLOSE && param1 == 16)) {
        auto* form = reinterpret_cast<ServerForm*>(g_info.SendDlgMessage(dialog, DM_GETDLGDATA, 0, nullptr));
        PullServerForm(dialog, form);
    }
    return farform::detail::GuardProc(dialog, msg, param1, param2);
}

bool EditServer(StoredServer& server, bool create) {
    wchar_t name[128]{};
    wchar_t host[256]{};
    wchar_t port[16]{};
    wchar_t user[128]{};
    wchar_t key[520]{};
    wchar_t proxy[256]{};
    wchar_t password[256]{};
    wcsncpy_s(name, server.name.c_str(), _TRUNCATE);
    wcsncpy_s(host, server.host.c_str(), _TRUNCATE);
    wcsncpy_s(port, server.port.empty() ? L"22" : server.port.c_str(), _TRUNCATE);
    wcsncpy_s(user, server.user.c_str(), _TRUNCATE);
    const std::wstring keyShow = IdentityStore(server.keyFile);
    wcsncpy_s(key, keyShow.empty() ? server.vaultKey.c_str() : keyShow.c_str(), _TRUNCATE);
    wcsncpy_s(proxy, server.proxy.c_str(), _TRUNCATE);
    wcsncpy_s(password, server.password.c_str(), _TRUNCATE);

    const std::wstring title = farform::PaddedTitle(
        create ? L"Новый сервер" : (server.fromTeleport ? L"Teleport" : L"Сервер"));
    FarDialogItem items[18]{};
    farform::FillBox(items[0], farform::kWidth, 15, title.c_str());
    farform::FillLabel(items[1], 2, L"Имя:");
    farform::FillEdit(items[2], 2, farform::kEditX, farform::kInnerR, name, 127, false, DIF_FOCUS);
    farform::FillLabel(items[3], 4, L"Адрес:");
    farform::FillEdit(items[4], 4, farform::kEditX, farform::kInnerR, host, 255, false);
    farform::FillLabel(items[5], 6, L"Порт:");
    farform::FillEdit(items[6], 6, farform::kEditX, 18, port, 5, false);
    farform::FillLabel(items[7], 6, L"Логин:", 21);
    farform::FillEdit(items[8], 6, 29, farform::kInnerR, user, 127, false);
    farform::FillLabel(items[9], 8, L"Ключ:");
    farform::FillEdit(items[10], 8, farform::kEditX, farform::kInnerR, key, 519, false);
    farform::FillLabel(items[11], 10, L"Прокси:");
    farform::FillEdit(items[12], 10, farform::kEditX, 32, proxy, 255, false);
    farform::FillLabel(items[13], 10, L"Пароль:", 35);
    farform::FillEdit(items[14], 10, 44, farform::kInnerR, password, 255, true);
    farform::FillSeparator(items[15], 11);
    farform::FillButton(items[16], 12, L"Сохранить", true);
    farform::FillButton(items[17], 12, L"Отмена", false);

    if (!create && server.fromTeleport) {
        items[2].Flags = DIF_READONLY;
        items[4].Flags |= DIF_READONLY;
        items[6].Flags |= DIF_READONLY;
        items[8].Flags |= DIF_FOCUS;
    }

    ServerForm form{name, host, port, user, key, proxy, password};
    HANDLE dialog = farform::OpenGuarded(g_info, MainGuid, ServerDialogGuid, items, 18,
                                        farform::kWidth, 15, 17, ServerDlgProc);
    if (!dialog || dialog == INVALID_HANDLE_VALUE) {
        Say(L"Не удалось открыть диалог сервера.");
        return false;
    }
    g_info.SendDlgMessage(dialog, DM_SETDLGDATA, 0, &form);
    const intptr_t result = g_info.DialogRun(dialog);
    const bool saved = result == 16;
    if (saved) PullServerForm(dialog, &form);
    g_info.DialogFree(dialog);
    if (!saved) return false;
    if (name[0] == L'\0' || host[0] == L'\0') {
        Say(L"Нужны имя и адрес сервера.");
        return false;
    }
    server.name = name;
    server.host = host;
    server.port = port[0] ? port : L"22";
    server.user = user;
    server.proxy = proxy;
    server.fromConfig = false;
    const std::wstring resolved = ResolveIdentityPath(key);
    if (FileExists(resolved)) {
        server.keyFile = IdentityStore(key);
        server.vaultKey.clear();
    } else {
        server.vaultKey = key;
        server.keyFile.clear();
    }
    if (password[0]) {
        server.password = password;
        if (VaultPluginPresent() && VaultStorePassword(name, password)) {
            server.vaultPassword = name;
            server.password.clear();
        }
    }
    return true;
}

void ShowHost(const StoredServer& site) {
    const std::wstring name = L"Имя: " + site.name;
    const std::wstring source = site.fromTeleport ? L"Источник: Teleport (tsh)"
        : site.fromConfig ? L"Источник: ~/.ssh/config"
        : L"Источник: SSH-плагин";
    const std::wstring host = L"Адрес: " + site.host;
    const std::wstring port = L"Порт: " + (site.port.empty() ? L"22" : site.port);
    const std::wstring user = L"Логин: " + (site.user.empty() ? L"(по умолчанию)" : site.user);
    std::wstring key = L"Ключ: ";
    if (!site.keyFile.empty()) key += IdentityLabel(site.keyFile);
    else if (!site.vaultKey.empty()) key += site.vaultKey + L" (vault)";
    else if (!site.identityFiles.empty()) key += IdentityLabel(site.identityFiles.front());
    else if (!site.keyLabel.empty()) key += site.keyLabel;
    else key += L"(ssh-agent)";
    const std::wstring proxy = site.fromTeleport ? L"Прокси: tsh proxy ssh"
        : site.proxy.empty() ? L"Прокси: —" : L"Прокси: " + site.proxy;
    const std::wstring vault = site.vaultPassword.empty() ? L"Пароль vault: —"
        : L"Пароль vault: " + site.vaultPassword;
    const wchar_t* lines[] = {
        L"Сервер", L"",
        name.c_str(), source.c_str(), host.c_str(), port.c_str(),
        user.c_str(), key.c_str(), proxy.c_str(), vault.c_str(),
    };
    g_info.Message(MainGuid, MenuGuid, FMSG_LEFTALIGN | FMSG_MB_OK, nullptr, lines, 10, 1);
}

bool Upsert(StoredServer server) {
    auto servers = LoadServers();
    const std::wstring key = Lower(server.name);
    bool replaced = false;
    for (auto& item : servers) {
        if (Lower(item.name) != key) continue;
        item = server;
        replaced = true;
        break;
    }
    if (!replaced) servers.push_back(server);
    return SaveServers(servers);
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

const intptr_t kHotkeyCreate = 1;
const intptr_t kHotkeyEdit = 2;
HANDLE g_hotkeyPanel = nullptr;
bool g_hotkeyPending = false;

void RefreshPanel() {
    if (!g_info.PanelControl) return;
    g_info.PanelControl(PANEL_ACTIVE, FCTL_UPDATEPANEL, 1, nullptr);
    g_info.PanelControl(PANEL_ACTIVE, FCTL_REDRAWPANEL, 0, nullptr);
}

void RunSshHotkey(intptr_t action) {
    g_hotkeyPending = false;
    auto* panel = PanelOf(g_hotkeyPanel);
    if (!panel || !panel->sites) return;
    if (action == kHotkeyCreate) {
        StoredServer server;
        if (EditServer(server, true)) Upsert(server);
    } else {
        const std::wstring name = CurrentName();
        const StoredServer* site = panel->Find(name);
        if (site) {
            StoredServer edited = *site;
            const bool tsh = site->fromTeleport;
            if (EditServer(edited, false)) {
                if (tsh) edited.fromTeleport = true;
                Upsert(edited);
            }
        }
    }
    panel->cached = CombinedSites();
    RefreshPanel();
}

bool PostSshHotkey(HANDLE hPanel, intptr_t action) {
    if (g_hotkeyPending) return true;
    g_hotkeyPending = true;
    g_hotkeyPanel = hPanel;
    if (g_info.AdvControl && g_info.AdvControl(MainGuid, ACTL_SYNCHRO, 0, reinterpret_cast<void*>(action)))
        return true;
    RunSshHotkey(action);
    return true;
}

bool CurrentFile(std::wstring& name, bool& directory) {
    if (!g_info.PanelControl) return false;
    FarGetPanelItem query{};
    query.StructSize = sizeof(query);
    const intptr_t bytes = g_info.PanelControl(PANEL_ACTIVE, kGetCurrentItem, 0, &query);
    if (bytes <= 0) return false;
    std::vector<uint8_t> buffer(static_cast<size_t>(bytes));
    auto* item = reinterpret_cast<PluginPanelItem*>(buffer.data());
    query.Size = static_cast<size_t>(bytes);
    query.Item = item;
    if (g_info.PanelControl(PANEL_ACTIVE, kGetCurrentItem, 0, &query) == 0 || !item->FileName) return false;
    name = item->FileName;
    directory = (item->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return true;
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

const wchar_t* ViewerFor(const std::wstring& name) {
    const auto dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return nullptr;
    const std::wstring ext = Lower(name.substr(dot));
    if (ext == L".md" || ext == L".markdown" || ext == L".mdown" || ext == L".mkd")
        return L"E4B1428A-16D3-4F8C-9B2A-7D5F1C3E9A4B";
    if (ext == L".json" || ext == L".yaml" || ext == L".yml" || ext == L".xml")
        return L"C8E14A20-6F3B-4D91-A5E7-1B9C0D4F6A82";
    return nullptr;
}

bool OpenInViewer(SshPanel* panel, const std::wstring& name) {
    const wchar_t* pluginId = ViewerFor(name);
    if (!pluginId) return false;
    if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) return false;
    if (!RunMacro(L"local id = ...; return Plugin.Exist(id)", pluginId, nullptr))
        return false;

    wchar_t temp[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp)) return false;
    std::wstring dir = temp;
    if (dir.empty() || dir.back() != L'\\') dir += L'\\';
    dir += L"FarSshView";
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring local = JoinLocal(dir, name.c_str());
    std::wstring error;
    if (!panel->connection.Download(JoinRemote(panel->remote, Utf8(name)), local, false, error)) {
        if (!error.empty()) Say(error.c_str());
        return true;
    }
    // Plugin.Command yields. A direct macro call cannot yield across the C boundary,
    // so queue it; Far runs the queued macro after this key returns.
    if (!RunMacro(
            L"local id, path = ...; return mf.postmacro(function(a, b) Plugin.Command(a, b) end, id, path)",
            pluginId, local.c_str()))
        return false;
    return true;
}

void FillItem(PluginPanelItem& item, const std::wstring& name, bool directory, unsigned long long size, unsigned long mtime) {
    item.FileName = name.c_str();
    item.FileAttributes = directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    item.FileSize = size;
    item.AllocationSize = size;
    if (mtime) {
        UnixToFileTime(mtime, &item.LastWriteTime);
        item.CreationTime = item.LastWriteTime;
        item.LastAccessTime = item.LastWriteTime;
        item.ChangeTime = item.LastWriteTime;
    }
}

Listing* Publish(std::vector<Row> rows, std::vector<PluginPanelItem> items) {
    auto* listing = new Listing{std::move(rows), std::move(items)};
    for (size_t i = 0; i < listing->rows.size() && i < listing->items.size(); ++i) {
        listing->rows[i].columns[0] = listing->rows[i].host.c_str();
        listing->rows[i].columns[1] = listing->rows[i].key.c_str();
        listing->items[i].FileName = listing->rows[i].name.c_str();
        if (!listing->rows[i].host.empty()) {
            listing->items[i].CustomColumnData = listing->rows[i].columns;
            listing->items[i].CustomColumnNumber = 2;
        }
    }
    g_listings[listing->items.data()] = listing;
    return listing;
}

void RemoveLocal(const std::wstring& path, bool directory) {
    if (!directory) {
        DeleteFileW(path.c_str());
        return;
    }
    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW((path + L"\\*").c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0) continue;
            RemoveLocal(path + L"\\" + found.cFileName, (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    RemoveDirectoryW(path.c_str());
}

}  // namespace

HANDLE OpenSshPanel(const wchar_t* siteName) {
    auto* panel = new SshPanel();
    panel->ShowSites();
    if (siteName && *siteName) {
        const StoredServer* site = panel->Find(siteName);
        if (!site) {
            Say(L"Сервер не найден. Добавьте его по F7.");
        } else {
            std::wstring error;
            if (panel->connection.Connect(*site, error)) {
                panel->OpenSite(*site);
            } else if (!error.empty()) {
                Say(error.c_str());
            }
        }
    }
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
    if (panel->sites && TshAvailable())
        panel->title = TshLoggedIn() ? L" SSH · Teleport " : L" SSH · F6 tsh login ";
    info->PanelTitle = panel->title.c_str();
    info->CurDir = panel->curDir.c_str();
    static const wchar_t* siteTitles[] = { L"Имя", L"Адрес", L"Ключ" };
    static const wchar_t* fileTitles[] = { L"Имя", L"Размер", L"Дата" };
    static const PanelMode sitesMode{ L"N,C0,C1", L"0,16,8", siteTitles, L"N,C0", L"0,0", 0 };
    static const PanelMode fileMode{ L"N,S,DM", L"0,12,14", fileTitles, L"N,S", L"0,10", 0 };
    static KeyLabel siteKeys[] = {
        {{VK_F2, 0}, L"Терм", L"SSH-терминал"},
        {{VK_F3, 0}, L"Инфо", L"Карточка сервера"},
        {{VK_F4, 0}, L"Сервер", L"Изменить сервер"},
        {{VK_F5, 0}, L"", L""},
        {{VK_F6, 0}, TshAvailable() ? L"tsh" : L"", TshAvailable() ? L"tsh login" : L""},
        {{VK_F7, 0}, L"Новый", L"Новый сервер"},
        {{VK_F8, 0}, L"Удал", L"Удалить сервер"},
        {{VK_F4, SHIFT_PRESSED}, L"Новый", L"Новый сервер"},
    };
    static KeyLabel fileKeys[] = {
        {{VK_F2, 0}, L"Терм", L"SSH-терминал"},
        {{VK_F3, 0}, L"Просм", L"Просмотр"},
        {{VK_F4, 0}, L"Редак", L"Редактирование"},
        {{VK_F5, 0}, L"Копир", L"Копирование"},
        {{VK_F6, 0}, L"Перен", L"Переименование"},
        {{VK_F7, 0}, L"Папка", L"Создать папку"},
        {{VK_F8, 0}, L"Удал", L"Удаление"},
    };
    static KeyTitles siteBar{ std::size(siteKeys), siteKeys };
    static KeyTitles fileBar{ std::size(fileKeys), fileKeys };
    info->PanelModesArray = panel->sites ? &sitesMode : &fileMode;
    info->PanelModesNumber = 1;
    info->StartPanelMode = L'0';
    info->StartSortMode = 2;
    info->StartSortOrder = 0;
    info->KeyBar = panel->sites ? &siteBar : &fileBar;
}

intptr_t WINAPI GetFindDataW(GetFindDataInfo* info) {
    auto* panel = PanelOf(info ? info->hPanel : nullptr);
    if (!info || !panel) return 0;
    std::vector<Row> rows;
    std::vector<PluginPanelItem> items;
    if (panel->sites) {
        panel->cached = CombinedSites();
        for (const auto& site : panel->cached) {
            Row row;
            row.name = site.name;
            row.host = (site.user.empty() ? L"" : site.user + L"@") + site.host;
            if (!site.port.empty() && site.port != L"22") row.host += L":" + site.port;
            if (!site.proxy.empty()) row.host += L" via " + site.proxy;
            if (site.fromTeleport) row.key = L"tsh";
            else if (site.fromConfig) row.key = L"config";
            else row.key = site.keyLabel;
            PluginPanelItem item{};
            FillItem(item, row.name, true, 0, 0);
            rows.push_back(std::move(row));
            items.push_back(item);
        }
    } else {
        std::vector<RemoteEntry> entries;
        std::wstring error;
        if (!panel->connection.List(panel->remote, entries, error)) {
            if ((info->OpMode & kSilent) == 0 && !error.empty()) Say(error.c_str());
            return 0;
        }
        for (const auto& entry : entries) {
            Row row;
            row.name = entry.name;
            PluginPanelItem item{};
            FillItem(item, row.name, entry.directory, entry.size, entry.mtime);
            rows.push_back(std::move(row));
            items.push_back(item);
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
        if (panel->sites) return 0;
        if (panel->remote.empty() || panel->remote == "/") {
            panel->ShowSites();
            return 1;
        }
        panel->remote = ParentRemote(panel->remote);
        panel->curDir = Wide(panel->remote);
        return 1;
    }
    if (panel->sites) {
        const auto slash = dir.find_last_of(L"\\/");
        const std::wstring name = slash == std::wstring::npos ? dir : dir.substr(slash + 1);
        const StoredServer* site = panel->Find(name);
        if (!site) return 0;
        std::wstring error;
        if (!panel->connection.Connect(*site, error)) {
            if ((info->OpMode & kSilent) == 0 && !error.empty()) Say(error.c_str());
            return 0;
        }
        panel->OpenSite(*site);
        return 1;
    }
    std::string next = JoinRemote(panel->remote, Utf8(dir));
    std::wstring error;
    std::vector<RemoteEntry> probe;
    if (!panel->connection.List(next, probe, error)) {
        if ((info->OpMode & kSilent) == 0 && !error.empty()) Say(error.c_str());
        return 0;
    }
    panel->remote = next;
    panel->curDir = Wide(next);
    return 1;
}

unsigned long long LocalBytes(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        unsigned long long sum = 0;
        WIN32_FIND_DATAW found{};
        HANDLE search = FindFirstFileW((path + L"\\*").c_str(), &found);
        if (search == INVALID_HANDLE_VALUE) return 0;
        do {
            if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0) continue;
            sum += LocalBytes(path + L"\\" + found.cFileName);
        } while (FindNextFileW(search, &found));
        FindClose(search);
        return sum;
    }
    ULARGE_INTEGER size;
    size.LowPart = fad.nFileSizeLow;
    size.HighPart = fad.nFileSizeHigh;
    return size.QuadPart;
}

intptr_t WINAPI GetFilesW(GetFilesInfo* info) {
    auto* panel = PanelOf(info ? info->hPanel : nullptr);
    if (!info || !panel || panel->sites || !info->DestPath) return 0;
    CopyMeter meter;
    meter.silent = (info->OpMode & kSilent) != 0;
    for (size_t i = 0; i < info->ItemsNumber; ++i) {
        const wchar_t* name = info->PanelItem[i].FileName;
        if (!name || wcscmp(name, L"..") == 0) continue;
        meter.total += info->PanelItem[i].FileSize;
    }
    CopyMeterBegin(meter);
    intptr_t ok = 1;
    for (size_t i = 0; i < info->ItemsNumber; ++i) {
        const wchar_t* name = info->PanelItem[i].FileName;
        if (!name || wcscmp(name, L"..") == 0) continue;
        std::wstring error;
        if (CopyMeterAborted(&meter, error)) {
            if ((info->OpMode & kSilent) == 0) Say(error.c_str());
            ok = 0;
            break;
        }
        CopyMeterFile(meter, name);
        const bool directory = (info->PanelItem[i].FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const std::string remote = JoinRemote(panel->remote, Utf8(name));
        if (!panel->connection.Download(remote, JoinLocal(info->DestPath, name), directory, error, &meter)) {
            if ((info->OpMode & kSilent) == 0 && !error.empty()) Say(error.c_str());
            ok = 0;
            break;
        }
        if (info->Move && !panel->connection.Remove(remote, directory, error)) {
            if ((info->OpMode & kSilent) == 0 && !error.empty()) Say(error.c_str());
            ok = 0;
            break;
        }
    }
    CopyMeterEnd();
    return ok;
}

intptr_t WINAPI PutFilesW(PutFilesInfo* info) {
    auto* panel = PanelOf(info ? info->hPanel : nullptr);
    if (!info || !panel || panel->sites || !info->SrcPath) return 0;
    CopyMeter meter;
    meter.silent = (info->OpMode & kSilent) != 0;
    for (size_t i = 0; i < info->ItemsNumber; ++i) {
        const wchar_t* name = info->PanelItem[i].FileName;
        if (!name || wcscmp(name, L"..") == 0) continue;
        unsigned long long size = info->PanelItem[i].FileSize;
        if (size == 0) size = LocalBytes(JoinLocal(info->SrcPath, name));
        meter.total += size;
    }
    CopyMeterBegin(meter);
    intptr_t ok = 1;
    for (size_t i = 0; i < info->ItemsNumber; ++i) {
        const wchar_t* name = info->PanelItem[i].FileName;
        if (!name || wcscmp(name, L"..") == 0) continue;
        std::wstring error;
        if (CopyMeterAborted(&meter, error)) {
            if ((info->OpMode & kSilent) == 0) Say(error.c_str());
            ok = 0;
            break;
        }
        CopyMeterFile(meter, name);
        const bool directory = (info->PanelItem[i].FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const std::wstring local = JoinLocal(info->SrcPath, name);
        const std::string remote = JoinRemote(panel->remote, Utf8(name));
        if (!panel->connection.Upload(local, remote, directory, error, &meter)) {
            if ((info->OpMode & kSilent) == 0 && !error.empty()) Say(error.c_str());
            ok = 0;
            break;
        }
        if (info->Move) RemoveLocal(local, directory);
    }
    CopyMeterEnd();
    return ok;
}

intptr_t WINAPI DeleteFilesW(const DeleteFilesInfo* info) {
    auto* panel = PanelOf(info ? info->hPanel : nullptr);
    if (!info || !panel) return 0;
    if (panel->sites) {
        auto servers = LoadServers();
        bool changed = false;
        for (size_t i = 0; i < info->ItemsNumber; ++i) {
            const wchar_t* name = info->PanelItem[i].FileName;
            if (!name) continue;
            const StoredServer* site = panel->Find(name);
            if (site && site->fromTeleport) {
                if ((info->OpMode & kSilent) == 0)
                    Say(L"Этот сервер из tsh ls и не удаляется отсюда.");
                continue;
            }
            if (site && site->fromConfig) {
                if ((info->OpMode & kSilent) == 0)
                    Say(L"Этот сервер задан в ~/.ssh/config и не удаляется отсюда.");
                continue;
            }
            const std::wstring key = Lower(name);
            const auto before = servers.size();
            servers.erase(std::remove_if(servers.begin(), servers.end(), [&](const StoredServer& item) {
                return Lower(item.name) == key;
            }), servers.end());
            changed = changed || servers.size() != before;
        }
        if (changed) SaveServers(servers);
        panel->cached = CombinedSites();
        return changed ? 1 : 0;
    }
    for (size_t i = 0; i < info->ItemsNumber; ++i) {
        const wchar_t* name = info->PanelItem[i].FileName;
        if (!name || wcscmp(name, L"..") == 0) continue;
        const bool directory = (info->PanelItem[i].FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        std::wstring error;
        if (!panel->connection.Remove(JoinRemote(panel->remote, Utf8(name)), directory, error)) {
            if ((info->OpMode & kSilent) == 0 && !error.empty()) Say(error.c_str());
            return 0;
        }
    }
    return 1;
}

intptr_t WINAPI MakeDirectoryW(MakeDirectoryInfo* info) {
    auto* panel = PanelOf(info ? info->hPanel : nullptr);
    if (!info || !panel || panel->sites || !info->Name || !*info->Name) return 0;
    std::wstring error;
    if (!panel->connection.Mkdir(JoinRemote(panel->remote, Utf8(info->Name)), error)) {
        if ((info->OpMode & kSilent) == 0 && !error.empty()) Say(error.c_str());
        return 0;
    }
    return 1;
}

intptr_t WINAPI ProcessSynchroEventW(const ProcessSynchroEventInfo* info) {
    if (!info || info->Event != SE_COMMONSYNCHRO) return 0;
    RunSshHotkey(reinterpret_cast<intptr_t>(info->Param));
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

    if (panel->sites && !ctrl && !alt &&
        (key.wVirtualKeyCode == VK_F7 || (key.wVirtualKeyCode == VK_F4 && shift))) {
        PostSshHotkey(info->hPanel, kHotkeyCreate);
        return 1;
    }
    if (panel->sites && key.wVirtualKeyCode == VK_F4 && !shift && !ctrl && !alt) {
        PostSshHotkey(info->hPanel, kHotkeyEdit);
        return 1;
    }

    const bool terminalKey =
        (key.wVirtualKeyCode == VK_F2 && !shift && !ctrl && !alt) ||
        (key.wVirtualKeyCode == VK_RETURN && shift && !ctrl && !alt);
    if (terminalKey) {
        const StoredServer* site = nullptr;
        StoredServer owned;
        if (panel->sites) {
            const std::wstring name = CurrentName();
            if (!name.empty() && name != L"..") site = panel->Find(name);
        } else if (!panel->current.host.empty()) {
            owned = panel->current;
            site = &owned;
        }
        if (site) {
            std::wstring error;
            if (!OpenSshTerminal(*site, error) && !error.empty()) Say(error.c_str());
        }
        return 1;
    }
    if (panel->sites && key.wVirtualKeyCode == VK_F6 && !shift && !ctrl && !alt) {
        if (TshAvailable()) {
            std::wstring error;
            if (!TshLogin(error) && !error.empty()) Say(error.c_str());
            if (g_info.PanelControl) {
                g_info.PanelControl(PANEL_ACTIVE, FCTL_UPDATEPANEL, 1, nullptr);
                g_info.PanelControl(PANEL_ACTIVE, FCTL_REDRAWPANEL, 0, nullptr);
            }
        }
        return 1;
    }
    if (panel->sites && key.wVirtualKeyCode == VK_F5 && !ctrl && !alt)
        return 1;
    if (panel->sites && key.wVirtualKeyCode == VK_F3 && !shift && !ctrl && !alt) {
        const std::wstring name = CurrentName();
        const StoredServer* site = panel->Find(name);
        if (site) ShowHost(*site);
        return 1;
    }
    if (!panel->sites && key.wVirtualKeyCode == VK_F3 && !shift && !ctrl && !alt) {
        std::wstring name;
        bool directory = false;
        if (!CurrentFile(name, directory) || directory || name == L".." || name == L".")
            return 0;
        if (OpenInViewer(panel, name))
            return 1;
    }
    if (!panel->sites && key.wVirtualKeyCode == VK_F6 && !ctrl && !alt) {
        const std::wstring name = CurrentName();
        if (name.empty() || name == L"..") return 1;
        const std::wstring next = AskText(L"Переименовать", name.c_str(), false);
        if (!next.empty() && next != name) {
            std::wstring error;
            if (!panel->connection.Rename(JoinRemote(panel->remote, Utf8(name)), JoinRemote(panel->remote, Utf8(next)), error) &&
                !error.empty())
                Say(error.c_str());
        }
        if (g_info.PanelControl) g_info.PanelControl(PANEL_ACTIVE, FCTL_REDRAWPANEL, 0, nullptr);
        return 1;
    }
    return 0;
}

}
