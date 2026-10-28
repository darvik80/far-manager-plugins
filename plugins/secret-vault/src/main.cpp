#include "far/disk_hotkey.hpp"
#include "far/plugin.hpp"
#include "guid.hpp"
#include "panel.hpp"
#include "text.hpp"
#include "ui.hpp"
#include "sync.hpp"
#include "vault.hpp"
#include "nk_secure.hpp"

#include <string>

namespace {

struct OpenMacroInfo {
    size_t StructSize;
    size_t Count;
    void* Values;
};

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

struct FarMacroCall {
    size_t StructSize;
    size_t Count;
    MacroValue* Values;
    void (WINAPI* Callback)(void*, MacroValue*, size_t);
    void* CallbackData;
};

std::wstring MacroString(const OpenMacroInfo* info, size_t index) {
    if (!info || index >= info->Count || !info->Values) return {};
    const auto* values = static_cast<const MacroValue*>(info->Values);
    if (values[index].Type != 2 || !values[index].String) return {};
    return values[index].String;
}

thread_local std::wstring g_reply;
thread_local MacroValue g_value{};
thread_local FarMacroCall g_call{};

void WINAPI FreeReply(void*, MacroValue*, size_t) {
    g_reply.clear();
}

HANDLE ReplyString(const std::wstring& text) {
    g_reply = text;
    g_value = {};
    g_value.Type = 2;
    g_value.String = g_reply.c_str();
    g_call = {};
    g_call.StructSize = sizeof(g_call);
    g_call.Count = 1;
    g_call.Values = &g_value;
    g_call.Callback = FreeReply;
    return &g_call;
}

bool CallerAllowed(const std::wstring& guid) {
    const std::wstring id = Lower(guid);
    return id == L"c3a91e47-5b28-4d6f-8e14-2f7a6c9d0b51";
}

HANDLE HandleMacro(const OpenInfo* info) {
    const auto* macro = info && info->Data ? reinterpret_cast<const OpenMacroInfo*>(info->Data) : nullptr;
    if (!CallerAllowed(MacroString(macro, 0))) return ReplyString(L"");
    const std::wstring verb = Lower(MacroString(macro, 1));
    VaultData data;
    std::wstring error;
    if (!LoadVault(data, error)) return ReplyString(L"");

    if (verb == L"list") {
        const std::wstring kind = Lower(MacroString(macro, 2));
        std::wstring names;
        if (kind == L"keys") {
            for (const auto& item : data.keys) {
                if (!names.empty()) names += L"\n";
                names += item.name;
            }
        } else {
            for (const auto& item : data.passwords) {
                if (!names.empty()) names += L"\n";
                names += item.name;
            }
        }
        return ReplyString(names);
    }
    if (verb == L"lookup") {
        const std::wstring kind = Lower(MacroString(macro, 2));
        const std::wstring name = MacroString(macro, 3);
        if (kind == L"pem") {
            const auto* item = FindKey(data, name);
            return ReplyString(item ? item->privateKey : L"");
        }
        if (kind == L"pub") {
            const auto* item = FindKey(data, name);
            return ReplyString(item ? item->publicKey : L"");
        }
        if (kind == L"key") {
            const auto* item = FindKey(data, name);
            if (!item) return ReplyString(L"");
            const std::wstring path = WriteTempKey(*item, error);
            return ReplyString(path);
        }
        const auto* item = FindPassword(data, name);
        return ReplyString(item ? item->secret : L"");
    }
    if (verb == L"store") {
        const std::wstring kind = Lower(MacroString(macro, 2));
        const std::wstring name = MacroString(macro, 3);
        const std::wstring secret = MacroString(macro, 4);
        if (name.empty()) return ReplyString(L"");
        if (kind == L"key") {
            KeyRecord* item = FindKey(data, name);
            if (!item) {
                KeyRecord created;
                created.id = NewRecordId();
                created.name = name;
                created.privateKey = secret;
                created.updatedAt = IsoUtcNow();
                data.keys.push_back(std::move(created));
            } else if (!secret.empty()) {
                item->privateKey = secret;
                item->updatedAt = IsoUtcNow();
            }
        } else {
            PasswordRecord* item = FindPassword(data, name);
            if (!item) {
                PasswordRecord created;
                created.id = NewRecordId();
                created.name = name;
                created.secret = secret;
                created.updatedAt = IsoUtcNow();
                data.passwords.push_back(std::move(created));
            } else if (!secret.empty()) {
                item->secret = secret;
                item->updatedAt = IsoUtcNow();
            }
        }
        SaveVault(data, error);
        return ReplyString(name);
    }
    return ReplyString(L"");
}

}  // namespace

extern "C" {

void WINAPI SetStartupInfoW(const PluginStartupInfo* info) {
    if (info) g_info = *info;
    far_disk_hotkey::EnsureDriveHotkey(MainGuid, MenuGuid, L'7');
}

void WINAPI GetGlobalInfoW(GlobalInfo* info) {
    info->StructSize = sizeof(GlobalInfo);
    info->MinFarVersion.Major = FARMANAGERVERSION_MAJOR;
    info->MinFarVersion.Minor = FARMANAGERVERSION_MINOR;
    info->MinFarVersion.Revision = FARMANAGERVERSION_REVISION;
    info->MinFarVersion.Build = FARMANAGERVERSION_BUILD;
    info->MinFarVersion.Stage = FARMANAGERVERSION_STAGE;
    info->Version.Major = 1;
    info->Version.Minor = 5;
    info->Version.Revision = 0;
    info->Version.Build = 0;
    info->Version.Stage = VS_RELEASE;
    info->Guid = MainGuid;
    info->Title = L"Secret Vault";
    info->Description = L"Encrypted passwords and OpenSSH keys";
    info->Author = L"Far Plugins";
}

int WINAPI GetMinFarVersionW(void) {
    return FARMANAGERVERSION;
}

void WINAPI GetPluginInfoW(PluginInfo* info) {
    info->StructSize = sizeof(PluginInfo);
    info->Flags = 0;
    static const GUID menuGuid = MenuGuid;
    static const wchar_t* menuStrings[] = { L"Secret Vault" };
    info->PluginMenu.Guids = &menuGuid;
    info->PluginMenu.Strings = menuStrings;
    info->PluginMenu.Count = 1;
    info->DiskMenu.Guids = &menuGuid;
    info->DiskMenu.Strings = menuStrings;
    info->DiskMenu.Count = 1;
    static const GUID configGuid = MenuGuid;
    static const wchar_t* configStrings[] = { L"Secret Vault" };
    info->PluginConfig.Guids = &configGuid;
    info->PluginConfig.Strings = configStrings;
    info->PluginConfig.Count = 1;
    info->CommandPrefix = L"secret";
}

HANDLE WINAPI OpenW(const OpenInfo* info) {
    if (info && info->OpenFrom == OPEN_FROMMACRO)
        return HandleMacro(info);
    return OpenVaultPanel();
}

intptr_t WINAPI ConfigureW(const ConfigureInfo*) {
    SyncSettings settings;
    LoadSyncSettings(settings);
    if (!EditSyncSettings(settings.baseUrl, settings.token)) return 1;
    if (!SaveSyncSettings(settings)) {
        Say(L"Не удалось сохранить PAT (DPAPI).");
        return 1;
    }
    const std::wstring line = L"Файл: " + VaultPath();
    const wchar_t* items[] = {
        L"Secret Vault",
        L"",
        L"Каталоги Пароли и Ключи. Shift+Alt+F9 — синк с NoteKeeper (HTTPS, PAT: vault).",
        L"F4 изменить, F7 / Shift+F4 создать, F5 в буфер, F8 удалить.",
        L"Alt+F5 экспорт, Alt+F6 импорт.",
        line.c_str(),
    };
    g_info.Message(MainGuid, MenuGuid, FMSG_LEFTALIGN | FMSG_MB_OK, nullptr, items, 6, 1);
    return 1;
}

void WINAPI ExitFARW(const ExitInfo*) {
    NkWipeTempPrefix(L"FarVault", L"key");
    NkWipeTempPrefix(L"FarVault", L"svk");
}

}
