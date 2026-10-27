#include "far/plugin.hpp"
#include "guid.hpp"
#include "markdown_parser.hpp"
#include "viewer.hpp"

#include <string>
#include <vector>
#include <cstdio>
#include <filesystem>
#include <algorithm>

static PluginStartupInfo g_info{};
static FarStandardFunctions* g_fsf = nullptr;

static void ReadFileLines(const std::wstring& path, std::vector<std::wstring>& out) {
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) return;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    std::string raw(size > 0 ? static_cast<size_t>(size) : 0, '\0');
    if (size > 0) fread(raw.data(), 1, raw.size(), file);
    fclose(file);

    if (raw.size() >= 3 &&
        (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF) {
        raw.erase(0, 3);
    }

    int wneeded = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), nullptr, 0);
    if (wneeded <= 0) {
        wneeded = MultiByteToWideChar(CP_ACP, 0, raw.c_str(), (int)raw.size(), nullptr, 0);
        std::wstring wline(wneeded, L'\0');
        MultiByteToWideChar(CP_ACP, 0, raw.c_str(), (int)raw.size(), &wline[0], wneeded);
        raw.clear();
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wline.c_str(), (int)wline.size(), nullptr, 0, nullptr, nullptr);
        raw.resize(bytes);
        WideCharToMultiByte(CP_UTF8, 0, wline.c_str(), (int)wline.size(), &raw[0], bytes, nullptr, nullptr);
        wneeded = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), nullptr, 0);
    }

    std::wstring content(wneeded, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), &content[0], wneeded);

    out.clear();
    size_t start = 0;
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == L'\n') {
            std::wstring line = content.substr(start, i - start);
            if (!line.empty() && line.back() == L'\r')
                line.pop_back();
            out.push_back(line);
            start = i + 1;
        }
    }
    if (start <= content.size()) {
        std::wstring line = content.substr(start);
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        if (!line.empty() || !out.empty())
            out.push_back(line);
    }
}

static std::wstring GetCurrentFilePath() {
    PanelInfo pi{};
    pi.StructSize = sizeof(PanelInfo);
    if (g_info.PanelControl(PANEL_ACTIVE, FCTL_GETPANELINFO, 0, &pi) == 0)
        return {};

    FarPanelDirectory dirInfo{};
    dirInfo.StructSize = sizeof(FarPanelDirectory);
    dirInfo.Name = nullptr;
    intptr_t size = g_info.PanelControl(PANEL_ACTIVE, FCTL_GETPANELDIRECTORY, 0, &dirInfo);
    if (size <= 0) return {};

    std::vector<uint8_t> buf(size);
    auto* pDir = reinterpret_cast<FarPanelDirectory*>(buf.data());
    pDir->StructSize = sizeof(FarPanelDirectory);
    pDir->Name = nullptr;
    g_info.PanelControl(PANEL_ACTIVE, FCTL_GETPANELDIRECTORY, size, pDir);

    std::wstring dir = pDir->Name ? pDir->Name : L"";

    if (pi.CurrentItem >= pi.ItemsNumber) return {};

    FarGetPanelItem gpi{};
    gpi.StructSize = sizeof(FarGetPanelItem);
    gpi.Size = 0;
    gpi.Item = nullptr;
    size = g_info.PanelControl(PANEL_ACTIVE, FCTL_GETPANELITEM, pi.CurrentItem, &gpi);
    if (size <= 0) return {};

    std::vector<uint8_t> ibuf(size);
    auto* pItem = reinterpret_cast<PluginPanelItem*>(ibuf.data());
    gpi.Size = size;
    gpi.Item = pItem;
    g_info.PanelControl(PANEL_ACTIVE, FCTL_GETPANELITEM, pi.CurrentItem, &gpi);

    if (!pItem->FileName) return {};
    if (pItem->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) return {};

    std::wstring path = dir;
    if (!path.empty() && path.back() != L'\\')
        path += L'\\';
    path += pItem->FileName;
    return path;
}

static bool IsMarkdownFile(const std::wstring& path) {
    auto ext = std::filesystem::path(path).extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext == L".md" || ext == L".markdown" || ext == L".mdown" || ext == L".mkd";
}

extern "C" {

void WINAPI SetStartupInfoW(const PluginStartupInfo* Info) {
    g_info = *Info;
    g_fsf = Info->FSF;
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
    Info->Title = L"Markdown Viewer";
    Info->Description = L"View Markdown files with syntax highlighting";
    Info->Author = L"Far Plugins";
}

int WINAPI GetMinFarVersionW(void) {
    return FARMANAGERVERSION;
}

void WINAPI GetPluginInfoW(PluginInfo* Info) {
    Info->StructSize = sizeof(PluginInfo);
    Info->Flags = 0;

    static const GUID menuGuid = MenuGuid;
    static const wchar_t* menuStrings[] = { L"Markdown Viewer" };

    Info->PluginMenu.Guids = &menuGuid;
    Info->PluginMenu.Strings = menuStrings;
    Info->PluginMenu.Count = 1;

    Info->DiskMenu.Count = 0;
    Info->PluginConfig.Count = 0;
    Info->CommandPrefix = L"mdview";
}

HANDLE WINAPI OpenW(const OpenInfo* Info) {
    std::wstring path;

    if (Info->OpenFrom == OPEN_COMMANDLINE) {
        auto* cli = reinterpret_cast<const OpenCommandLineInfo*>(Info->Data);
        if (cli && cli->CommandLine)
            path = cli->CommandLine;
        if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
            path = path.substr(1, path.size() - 2);
    } else {
        path = GetCurrentFilePath();
    }

    if (path.empty() || !IsMarkdownFile(path)) {
        const wchar_t* msg[] = {
            L"Markdown Viewer",
            L"",
            L"Select a .md file to view.",
            L"",
            L"  Supported extensions:",
            L"  .md  .markdown  .mdown  .mkd",
        };
        g_info.Message(MainGuid, MenuGuid, FMSG_MB_OK | FMSG_LEFTALIGN,
                       nullptr, msg, 6, 1);
        return nullptr;
    }

    std::vector<std::wstring> rawLines;
    ReadFileLines(path, rawLines);

    if (rawLines.empty()) {
        const wchar_t* msg[] = {
            L"Markdown Viewer",
            L"",
            L"File is empty or could not be read.",
        };
        g_info.Message(MainGuid, MenuGuid, FMSG_MB_OK, nullptr, msg, 3, 1);
        return nullptr;
    }

    MarkdownParser parser;
    auto styledLines = parser.Parse(rawLines);

    std::wstring filename = std::filesystem::path(path).filename().wstring();
    ShowMarkdownViewer(g_info, styledLines, filename, path);

    return nullptr;
}

intptr_t WINAPI ConfigureW(const ConfigureInfo*) {
    const wchar_t* msg[] = {
        L"Markdown Viewer",
        L"",
        L"Version 1.0",
        L"",
        L"No configurable options yet.",
    };
    g_info.Message(MainGuid, MenuGuid, FMSG_MB_OK, nullptr, msg, 5, 1);
    return 1;
}

void WINAPI ExitFARW(const ExitInfo*) {
}

static intptr_t OpenMarkdownOnF3(const INPUT_RECORD& rec) {
    if (rec.EventType != KEY_EVENT)
        return 0;

    const KEY_EVENT_RECORD& key = rec.Event.KeyEvent;
    if (!key.bKeyDown || key.wVirtualKeyCode != VK_F3)
        return 0;

    const DWORD mods = key.dwControlKeyState & (
        LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED |
        LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED |
        SHIFT_PRESSED);
    if (mods)
        return 0;

    struct WindowType {
        size_t StructSize;
        int Type;
    } window{};
    window.StructSize = sizeof(window);
    constexpr int kGetWindowType = 28;
    constexpr int kPanels = 1;
    const intptr_t windowResult = g_info.AdvControl
        ? g_info.AdvControl(MainGuid, kGetWindowType, 0, &window)
        : 0;
    const bool panels = window.Type == kPanels || (window.Type == 0 && windowResult == kPanels);
    if (!panels)
        return 0;

    const std::wstring path = GetCurrentFilePath();
    if (!IsMarkdownFile(path))
        return 0;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        return 0;

    OpenInfo open{};
    open.StructSize = sizeof(OpenInfo);
    open.OpenFrom = OPEN_FILEPANEL;
    open.Guid = &MenuGuid;
    OpenW(&open);
    return 1;
}

intptr_t WINAPI ProcessConsoleInputW(ProcessConsoleInputInfo* Info) {
    if (!Info) return 0;
    return OpenMarkdownOnF3(Info->Rec);
}

intptr_t WINAPI ProcessPanelInputW(const ProcessPanelInputInfo* Info) {
    if (!Info) return 0;
    return OpenMarkdownOnF3(Info->Rec);
}

}
