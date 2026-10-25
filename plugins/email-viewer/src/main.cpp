#include "far/disk_hotkey.hpp"
#include "far/plugin.hpp"
#include "guid.hpp"
#include "ui.hpp"
#include "imap_client.hpp"

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

static PluginStartupInfo g_info{};
static std::vector<ImapAccount> g_accounts;
static int g_selectedAccount = -1;

static std::wstring GetConfigPath() {
    wchar_t tempPath[MAX_PATH];
    if (!GetEnvironmentVariableW(L"APPDATA", tempPath, MAX_PATH))
        return L"";
    return std::wstring(tempPath) + L"\\FarManager\\EmailViewer.ini";
}

static void LoadSettings() {
    auto path = GetConfigPath();
    std::ifstream file(path);
    if (!file.is_open()) return;

    ImapAccount current;
    bool inAccount = false;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        if (line == "[account]") {
            if (inAccount && !current.settings.host.empty()) {
                g_accounts.push_back(current);
            }
            current = ImapAccount();
            inAccount = true;
            continue;
        }

        if (!inAccount) continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto key = line.substr(0, eq);
        auto value = line.substr(eq + 1);

        if (key == "name") {
            int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0);
            current.name.resize(needed);
            MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), &current.name[0], needed);
        } else if (key == "host") {
            int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0);
            current.settings.host.resize(needed);
            MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), &current.settings.host[0], needed);
        } else if (key == "port") {
            current.settings.port = std::stoi(value);
        } else if (key == "login") {
            int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0);
            current.settings.login.resize(needed);
            MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), &current.settings.login[0], needed);
        } else if (key == "password") {
            int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0);
            current.settings.password.resize(needed);
            MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), &current.settings.password[0], needed);
        } else if (key == "ssl") {
            current.settings.useSSL = (value == "1");
        }
    }

    if (inAccount && !current.settings.host.empty()) {
        g_accounts.push_back(current);
    }
}

static void SaveSettings() {
    auto path = GetConfigPath();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    std::ofstream file(path);
    if (!file.is_open()) return;

    auto w2u = [](const std::wstring& w) {
        int needed = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(needed, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], needed, nullptr, nullptr);
        return s;
    };

    for (const auto& acc : g_accounts) {
        file << "[account]\n";
        file << "name=" << w2u(acc.name) << "\n";
        file << "host=" << w2u(acc.settings.host) << "\n";
        file << "port=" << acc.settings.port << "\n";
        file << "login=" << w2u(acc.settings.login) << "\n";
        file << "password=" << w2u(acc.settings.password) << "\n";
        file << "ssl=" << (acc.settings.useSSL ? "1" : "0") << "\n\n";
    }
}

extern "C" {

void WINAPI SetStartupInfoW(const PluginStartupInfo* Info) {
    g_info = *Info;
    LoadSettings();
    far_disk_hotkey::EnsureDriveHotkey(MainGuid, MenuGuid, L'5');
}

void WINAPI GetGlobalInfoW(GlobalInfo* Info) {
    Info->StructSize = sizeof(GlobalInfo);
    Info->MinFarVersion.Major = FARMANAGERVERSION_MAJOR;
    Info->MinFarVersion.Minor = FARMANAGERVERSION_MINOR;
    Info->MinFarVersion.Revision = FARMANAGERVERSION_REVISION;
    Info->MinFarVersion.Build = FARMANAGERVERSION_BUILD;
    Info->MinFarVersion.Stage = FARMANAGERVERSION_STAGE;
    Info->Version.Major = 1;
    Info->Version.Minor = 0;
    Info->Version.Revision = 0;
    Info->Version.Build = 1;
    Info->Version.Stage = VS_RELEASE;
    Info->Guid = MainGuid;
    Info->Title = L"Email Viewer";
    Info->Description = L"View emails via IMAP";
    Info->Author = L"Far Plugins";
}

int WINAPI GetMinFarVersionW(void) {
    return FARMANAGERVERSION;
}

void WINAPI GetPluginInfoW(PluginInfo* Info) {
    Info->StructSize = sizeof(PluginInfo);
    Info->Flags = 0;

    static const wchar_t* menuStrings[] = { L"Email Viewer" };

    Info->PluginMenu.Guids = &MenuGuid;
    Info->PluginMenu.Strings = menuStrings;
    Info->PluginMenu.Count = 1;

    Info->DiskMenu.Guids = &MenuGuid;
    Info->DiskMenu.Strings = menuStrings;
    Info->DiskMenu.Count = 1;

    Info->PluginConfig.Guids = &MenuGuid;
    Info->PluginConfig.Strings = menuStrings;
    Info->PluginConfig.Count = 1;
    Info->CommandPrefix = L"email";
}

HANDLE WINAPI OpenW(const OpenInfo* Info) {
    if (Info->OpenFrom == OPEN_PLUGINSMENU
        || Info->OpenFrom == OPEN_LEFTDISKMENU
        || Info->OpenFrom == OPEN_RIGHTDISKMENU) {
        if (g_accounts.empty()) {
            ImapAccount newAcc;
            newAcc.name = L"Default";
            if (!ShowSettingsDialog(g_info, newAcc)) return nullptr;
            g_accounts.push_back(newAcc);
            SaveSettings();
        }

        if (g_accounts.size() == 1) {
            g_selectedAccount = 0;
        } else {
            ShowAccountList(g_info, g_accounts, g_selectedAccount);
            if (g_selectedAccount < 0 || g_selectedAccount >= (int)g_accounts.size()) {
                return nullptr;
            }
        }

        ImapClient client;
        if (!client.connect(g_accounts[g_selectedAccount].settings)) {
            std::string err = client.getLastError();
            std::wstring werr(err.begin(), err.end());
            std::wstring msg = L"Не удалось подключиться:\n" + werr;
            const wchar_t* items[] = { L"Почта", msg.c_str(), L"Изменить настройки соединения?" };
            if (g_info.Message(MainGuid, ErrorGuid, FMSG_MB_YESNO | FMSG_ERRORTYPE, nullptr, items, 3, 2) == 0) {
                if (ShowSettingsDialog(g_info, g_accounts[g_selectedAccount]))
                    SaveSettings();
            }
            return nullptr;
        }

        std::string folder;
        ShowFolderList(g_info, client, folder);

        if (!folder.empty()) {
            ShowMessageList(g_info, client, folder);
        }
    }

    return nullptr;
}

intptr_t WINAPI ConfigureW(const ConfigureInfo*) {
    ConfigureAccounts(g_info, g_accounts);
    SaveSettings();
    return 1;
}

void WINAPI ExitFARW(const ExitInfo*) {
}

}
