#include "far/disk_hotkey.hpp"
#include "far/plugin.hpp"
#include "guid.hpp"
#include "panel.hpp"
#include "ui.hpp"
#include "nk_secure.hpp"
#include "nk_settings.hpp"

extern "C" {

void WINAPI SetStartupInfoW(const PluginStartupInfo* info) {
    if (info) g_info = *info;
    far_disk_hotkey::EnsureDriveHotkey(MainGuid, MenuGuid, L'8');
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
    info->Version.Build = 1;
    info->Version.Stage = VS_RELEASE;
    info->Guid = MainGuid;
    info->Title = L"NoteKeeper";
    info->Description = L"Notes and todos from NoteKeeper";
    info->Author = L"Far Plugins";
}

int WINAPI GetMinFarVersionW(void) {
    return FARMANAGERVERSION;
}

void WINAPI GetPluginInfoW(PluginInfo* info) {
    info->StructSize = sizeof(PluginInfo);
    info->Flags = 0;
    static const GUID menuGuid = MenuGuid;
    static const wchar_t* menuStrings[] = { L"NoteKeeper" };
    info->PluginMenu.Guids = &menuGuid;
    info->PluginMenu.Strings = menuStrings;
    info->PluginMenu.Count = 1;
    info->DiskMenu.Guids = &menuGuid;
    info->DiskMenu.Strings = menuStrings;
    info->DiskMenu.Count = 1;
    static const GUID configGuid = MenuGuid;
    static const wchar_t* configStrings[] = { L"NoteKeeper" };
    info->PluginConfig.Guids = &configGuid;
    info->PluginConfig.Strings = configStrings;
    info->PluginConfig.Count = 1;
    info->CommandPrefix = L"nk";
}

HANDLE WINAPI OpenW(const OpenInfo*) {
    return OpenNotePanel();
}

intptr_t WINAPI ConfigureW(const ConfigureInfo*) {
    NkSettings settings;
    NkLoadSettings(settings);
    if (!EditSyncSettings(settings.baseUrl, settings.token)) return 1;
    if (!NkSaveSettings(settings)) {
        Say(L"Не удалось сохранить PAT (DPAPI).");
        return 1;
    }
    if (!NkConfigured(settings)) {
        Say(L"Нужны URL сервера и PAT.");
        return 1;
    }
    const wchar_t* items[] = {
        L"NoteKeeper",
        L"",
        L"Панель Notes и Todos. HTTPS + PAT (scopes notes,todos). Связь проверяется при сохранении.",
        L"F4 править, F7 создать, F3 смотреть, F8 удалить.",
        L"На todo: F5 или Space — done.",
        L"SSH берёт пароль/ключ из vault, если Secret Vault молчит.",
    };
    g_info.Message(MainGuid, MenuGuid, FMSG_LEFTALIGN | FMSG_MB_OK, nullptr, items, 6, 1);
    return 1;
}

void WINAPI ExitFARW(const ExitInfo*) {
    NkWipeTempPrefix(L"FarNoteKeeper", L"vw");
    NkWipeTempPrefix(L"FarNoteKeeper", L"nt");
    NkWipeTempPrefix(L"FarNoteKeeper", L"td");
    NkWipeTempPrefix(L"FarNoteKeeper", L"key");
}

}
