#include "far/disk_hotkey.hpp"
#include "far/plugin.hpp"
#include "guid.hpp"
#include "panel.hpp"
#include "session_store.hpp"
#include "sftp_client.hpp"
#include "ui.hpp"

extern "C" {

void WINAPI SetStartupInfoW(const PluginStartupInfo* info) {
    if (info) g_info = *info;
    StartupNetwork();
    far_disk_hotkey::EnsureDriveHotkey(MainGuid, MenuGuid, L'6');
}

void WINAPI GetGlobalInfoW(GlobalInfo* info) {
    info->StructSize = sizeof(GlobalInfo);
    info->MinFarVersion.Major = FARMANAGERVERSION_MAJOR;
    info->MinFarVersion.Minor = FARMANAGERVERSION_MINOR;
    info->MinFarVersion.Revision = FARMANAGERVERSION_REVISION;
    info->MinFarVersion.Build = FARMANAGERVERSION_BUILD;
    info->MinFarVersion.Stage = FARMANAGERVERSION_STAGE;
    info->Version.Major = 1;
    info->Version.Minor = 0;
    info->Version.Revision = 0;
    info->Version.Build = 2;
    info->Version.Stage = VS_RELEASE;
    info->Guid = MainGuid;
    info->Title = L"SSH";
    info->Description = L"SFTP panel for servers and ~/.ssh keys";
    info->Author = L"Far Plugins";
}

int WINAPI GetMinFarVersionW(void) {
    return FARMANAGERVERSION;
}

void WINAPI GetPluginInfoW(PluginInfo* info) {
    info->StructSize = sizeof(PluginInfo);
    info->Flags = 0;

    static const GUID menuGuid = MenuGuid;
    static const wchar_t* menuStrings[] = { L"SSH" };
    info->PluginMenu.Guids = &menuGuid;
    info->PluginMenu.Strings = menuStrings;
    info->PluginMenu.Count = 1;
    info->DiskMenu.Guids = &menuGuid;
    info->DiskMenu.Strings = menuStrings;
    info->DiskMenu.Count = 1;
    info->PluginConfig.Count = 0;
    info->CommandPrefix = L"ssh";
}

HANDLE WINAPI OpenW(const OpenInfo* info) {
    const wchar_t* site = nullptr;
    std::wstring owned;
    if (info && info->OpenFrom == OPEN_COMMANDLINE && info->Data) {
        auto* command = reinterpret_cast<const OpenCommandLineInfo*>(info->Data);
        if (command->CommandLine && command->CommandLine[0]) {
            owned = command->CommandLine;
            site = owned.c_str();
        }
    }
    return OpenSshPanel(site);
}

intptr_t WINAPI ConfigureW(const ConfigureInfo*) {
    const std::wstring line = L"Servers: " + ServersPath();
    const wchar_t* items[] = {
        L"SSH",
        L"",
        L"F3 view host, F4 edit, F7 new, F8 delete.",
        L"Enter opens SFTP. F2 or Shift+Enter opens an SSH terminal.",
        L"F5 copies files, F6 renames, F7 creates a folder.",
        line.c_str(),
    };
    g_info.Message(MainGuid, MenuGuid, FMSG_LEFTALIGN | FMSG_MB_OK, nullptr, items, 6, 1);
    return 1;
}

void WINAPI ExitFARW(const ExitInfo*) {
    ShutdownNetwork();
}

}
