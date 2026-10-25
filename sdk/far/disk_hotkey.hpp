#pragma once

// Far draws the digit in the disk menu from Profile\pluginhotkeys.db, not from
// the menu title. pluginkey is the plugin GUID, menuguid is the disk item GUID,
// type 0 is the drive menu. An existing hotkey is left as the user set it.

#include <windows.h>

#include <cstdio>
#include <string>

namespace far_disk_hotkey {

inline std::wstring GuidText(const GUID& guid) {
    wchar_t text[40];
    swprintf_s(
        text, L"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return text;
}

inline void WriteDriveHotkey(const wchar_t* dbPath, const std::wstring& pluginId, const std::wstring& menuId, const wchar_t* hotkey) {
    const auto sqlite = LoadLibraryW(L"winsqlite3.dll");
    if (!sqlite) return;

    using open16_fn = int(__cdecl*)(const void*, void**);
    using close_fn = int(__cdecl*)(void*);
    using busy_fn = int(__cdecl*)(void*, int);
    using prepare_fn = int(__cdecl*)(void*, const void*, int, void**, const void**);
    using bind_text_fn = int(__cdecl*)(void*, int, const void*, int, void(__cdecl*)(void*));
    using step_fn = int(__cdecl*)(void*);
    using finalize_fn = int(__cdecl*)(void*);

    const auto open16 = reinterpret_cast<open16_fn>(GetProcAddress(sqlite, "sqlite3_open16"));
    const auto close = reinterpret_cast<close_fn>(GetProcAddress(sqlite, "sqlite3_close"));
    const auto busy = reinterpret_cast<busy_fn>(GetProcAddress(sqlite, "sqlite3_busy_timeout"));
    const auto prepare = reinterpret_cast<prepare_fn>(GetProcAddress(sqlite, "sqlite3_prepare16_v2"));
    const auto bindText = reinterpret_cast<bind_text_fn>(GetProcAddress(sqlite, "sqlite3_bind_text16"));
    const auto step = reinterpret_cast<step_fn>(GetProcAddress(sqlite, "sqlite3_step"));
    const auto finalize = reinterpret_cast<finalize_fn>(GetProcAddress(sqlite, "sqlite3_finalize"));
    if (!open16 || !close || !busy || !prepare || !bindText || !step || !finalize) {
        FreeLibrary(sqlite);
        return;
    }

    void* db = nullptr;
    if (open16(dbPath, &db) != 0 || !db) {
        if (db) close(db);
        FreeLibrary(sqlite);
        return;
    }
    busy(db, 2000);

    void* stmt = nullptr;
    const wchar_t* sql =
        L"INSERT INTO pluginhotkeys(pluginkey, menuguid, type, hotkey) "
        L"VALUES (?1, ?2, 0, ?3) "
        L"ON CONFLICT(pluginkey, menuguid, type) DO UPDATE SET hotkey = excluded.hotkey "
        L"WHERE pluginhotkeys.hotkey IS NULL OR trim(pluginhotkeys.hotkey) = '';";
    if (prepare(db, sql, -1, &stmt, nullptr) == 0 && stmt) {
        const auto transient = reinterpret_cast<void(__cdecl*)(void*)>(-1);
        bindText(stmt, 1, pluginId.c_str(), -1, transient);
        bindText(stmt, 2, menuId.c_str(), -1, transient);
        bindText(stmt, 3, hotkey, -1, transient);
        step(stmt);
        finalize(stmt);
    }
    close(db);
    FreeLibrary(sqlite);
}

inline void EnsureDriveHotkey(const GUID& pluginId, const GUID& menuId, wchar_t key) {
    const std::wstring plugin = GuidText(pluginId);
    const std::wstring menu = GuidText(menuId);
    const wchar_t hotkey[2] = { key, 0 };

    wchar_t appData[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) > 0) {
        std::wstring path = appData;
        path += L"\\Far Manager\\Profile\\pluginhotkeys.db";
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            WriteDriveHotkey(path.c_str(), plugin, menu, hotkey);
    }

    wchar_t module[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, module, MAX_PATH) > 0) {
        std::wstring path = module;
        const auto slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            path.resize(slash);
            path += L"\\Profile\\pluginhotkeys.db";
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                WriteDriveHotkey(path.c_str(), plugin, menu, hotkey);
        }
    }
}

}  // namespace far_disk_hotkey
