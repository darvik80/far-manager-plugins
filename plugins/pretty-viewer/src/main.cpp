#include "far/plugin.hpp"
#include "format.hpp"
#include "guid.hpp"
#include "viewer.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static PluginStartupInfo g_info{};

static std::wstring ExtensionOf(const std::wstring& path) {
    std::wstring ext = std::filesystem::path(path).extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext;
}

static bool IsPrettyFile(const std::wstring& path) {
    const std::wstring ext = ExtensionOf(path);
    return ext == L".json" || ext == L".yaml" || ext == L".yml" || ext == L".xml";
}

static std::wstring DecodeBytes(const std::string& bytes) {
    if (bytes.size() >= 2 &&
        static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xFE) {
        const size_t chars = (bytes.size() - 2) / 2;
        std::wstring text(chars, L'\0');
        memcpy(text.data(), bytes.data() + 2, chars * 2);
        return text;
    }
    if (bytes.size() >= 2 &&
        static_cast<unsigned char>(bytes[0]) == 0xFE &&
        static_cast<unsigned char>(bytes[1]) == 0xFF) {
        const size_t chars = (bytes.size() - 2) / 2;
        std::wstring text(chars, L'\0');
        for (size_t i = 0; i < chars; ++i) {
            text[i] = static_cast<wchar_t>(
                (static_cast<unsigned char>(bytes[2 + i * 2]) << 8) |
                static_cast<unsigned char>(bytes[3 + i * 2]));
        }
        return text;
    }
    std::string raw = bytes;
    if (raw.size() >= 3 &&
        static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
    UINT codepage = CP_UTF8;
    if (needed <= 0) {
        codepage = CP_ACP;
        needed = MultiByteToWideChar(codepage, 0, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
    }
    if (needed <= 0) return {};
    std::wstring text(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(codepage, 0, raw.data(), static_cast<int>(raw.size()), text.data(), needed);
    return text;
}

static std::wstring ReadFile(const std::wstring& path) {
    const int needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
    std::string narrow(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), narrow.data(), needed, nullptr, nullptr);
    std::ifstream file(narrow, std::ios::binary);
    if (!file) return {};
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return DecodeBytes(bytes);
}

static std::wstring CurrentFile() {
    PanelInfo info{};
    info.StructSize = sizeof(PanelInfo);
    if (!g_info.PanelControl || g_info.PanelControl(PANEL_ACTIVE, FCTL_GETPANELINFO, 0, &info) == 0)
        return {};

    FarPanelDirectory probe{};
    probe.StructSize = sizeof(FarPanelDirectory);
    const intptr_t dirSize = g_info.PanelControl(PANEL_ACTIVE, FCTL_GETPANELDIRECTORY, 0, &probe);
    if (dirSize <= 0) return {};
    std::vector<uint8_t> dirBuf(static_cast<size_t>(dirSize));
    auto* dir = reinterpret_cast<FarPanelDirectory*>(dirBuf.data());
    dir->StructSize = sizeof(FarPanelDirectory);
    g_info.PanelControl(PANEL_ACTIVE, FCTL_GETPANELDIRECTORY, dirSize, dir);
    std::wstring folder = dir->Name ? dir->Name : L"";

    if (info.CurrentItem >= info.ItemsNumber) return {};
    FarGetPanelItem probeItem{};
    probeItem.StructSize = sizeof(FarGetPanelItem);
    const intptr_t itemSize = g_info.PanelControl(PANEL_ACTIVE, FCTL_GETPANELITEM, static_cast<intptr_t>(info.CurrentItem), &probeItem);
    if (itemSize <= 0) return {};
    std::vector<uint8_t> itemBuf(static_cast<size_t>(itemSize));
    probeItem.Size = static_cast<size_t>(itemSize);
    probeItem.Item = reinterpret_cast<PluginPanelItem*>(itemBuf.data());
    g_info.PanelControl(PANEL_ACTIVE, FCTL_GETPANELITEM, static_cast<intptr_t>(info.CurrentItem), &probeItem);
    if (!probeItem.Item->FileName) return {};
    if (probeItem.Item->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) return {};
    if (!folder.empty() && folder.back() != L'\\') folder += L'\\';
    folder += probeItem.Item->FileName;
    return folder;
}

static void Note(const wchar_t* text) {
    const wchar_t* items[] = { L"Pretty Viewer", text };
    if (g_info.Message) g_info.Message(MainGuid, MenuGuid, FMSG_MB_OK | FMSG_LEFTALIGN, nullptr, items, 2, 1);
}

static void OpenPath(const std::wstring& path) {
    if (path.empty() || !IsPrettyFile(path)) {
        Note(L"Выберите файл .json, .yaml, .yml или .xml");
        return;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        Note(L"Файл не найден");
        return;
    }
    const std::wstring text = ReadFile(path);
    if (text.empty()) {
        Note(L"Файл пуст или не читается");
        return;
    }
    ViewDoc doc = FormatDocument(text, ExtensionOf(path));
    ShowPrettyViewer(g_info, std::move(doc), SplitRaw(text), std::filesystem::path(path).filename().wstring());
}

extern "C" {

void WINAPI SetStartupInfoW(const PluginStartupInfo* info) {
    if (info) g_info = *info;
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
    info->Version.Build = 1;
    info->Version.Stage = VS_RELEASE;
    info->Guid = MainGuid;
    info->Title = L"Pretty Viewer";
    info->Description = L"Pretty view for JSON, YAML and XML";
    info->Author = L"Far Plugins";
}

int WINAPI GetMinFarVersionW(void) {
    return FARMANAGERVERSION;
}

void WINAPI GetPluginInfoW(PluginInfo* info) {
    info->StructSize = sizeof(PluginInfo);
    info->Flags = 0;
    static const GUID menuGuid = MenuGuid;
    static const wchar_t* menuStrings[] = { L"Pretty Viewer" };
    info->PluginMenu.Guids = &menuGuid;
    info->PluginMenu.Strings = menuStrings;
    info->PluginMenu.Count = 1;
    info->DiskMenu.Count = 0;
    info->PluginConfig.Count = 0;
    info->CommandPrefix = L"pretty";
}

HANDLE WINAPI OpenW(const OpenInfo* info) {
    std::wstring path;
    if (info && info->OpenFrom == OPEN_COMMANDLINE && info->Data) {
        const auto* line = reinterpret_cast<const OpenCommandLineInfo*>(info->Data);
        if (line && line->CommandLine) path = line->CommandLine;
    } else {
        path = CurrentFile();
    }
    OpenPath(path);
    return nullptr;
}

intptr_t WINAPI ConfigureW(const ConfigureInfo*) {
    Note(L"F3 на .json, .yaml, .yml, .xml. F2 исходный, F7 поиск, F8 тема.");
    return 1;
}

void WINAPI ExitFARW(const ExitInfo*) {}

static intptr_t OpenOnF3(const INPUT_RECORD& rec) {
    if (rec.EventType != KEY_EVENT) return 0;
    const KEY_EVENT_RECORD& key = rec.Event.KeyEvent;
    if (!key.bKeyDown || key.wVirtualKeyCode != VK_F3) return 0;
    const DWORD mods = key.dwControlKeyState & (
        LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED |
        LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED |
        SHIFT_PRESSED);
    if (mods) return 0;

    struct WindowType {
        size_t StructSize;
        int Type;
    } window{};
    window.StructSize = sizeof(window);
    constexpr int kGetWindowType = 28;
    constexpr int kPanels = 1;
    const intptr_t windowResult = g_info.AdvControl ? g_info.AdvControl(MainGuid, kGetWindowType, 0, &window) : 0;
    const bool panels = window.Type == kPanels || (window.Type == 0 && windowResult == kPanels);
    if (!panels) return 0;

    const std::wstring path = CurrentFile();
    if (!IsPrettyFile(path)) return 0;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return 0;
    OpenPath(path);
    return 1;
}

intptr_t WINAPI ProcessConsoleInputW(ProcessConsoleInputInfo* info) {
    if (!info) return 0;
    return OpenOnF3(info->Rec);
}

intptr_t WINAPI ProcessPanelInputW(const ProcessPanelInputInfo* info) {
    if (!info) return 0;
    return OpenOnF3(info->Rec);
}

}
