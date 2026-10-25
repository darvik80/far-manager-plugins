#include "far/plugin.hpp"
#include "guid.hpp"

#include <string>
#include <vector>
#include <fstream>
#include <algorithm>

static PluginStartupInfo g_info{};
static FarStandardFunctions* g_fsf = nullptr;

static void ReadFileLines(const std::wstring& path, std::vector<std::wstring>& out) {
    int needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), (int)path.size(), nullptr, 0, nullptr, nullptr);
    std::string pathStr(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), (int)path.size(), &pathStr[0], needed, nullptr, nullptr);

    std::ifstream file(pathStr, std::ios::binary);
    if (!file.is_open()) return;

    std::string raw((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());

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
    auto* pItem = reinterpret_cast<PanelItem*>(ibuf.data());
    gpi.Size = size;
    gpi.Item = pItem;
    g_info.PanelControl(PANEL_ACTIVE, FCTL_GETPANELITEM, pi.CurrentItem, &gpi);

    if (!pItem->FileName) return {};
    return dir + L"\\" + pItem->FileName;
}

static bool IsMarkdownFile(const std::wstring& path) {
    if (path.size() < 3) return false;
    auto ext = path.substr(path.find_last_of(L'.'));
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext == L".md" || ext == L".markdown" || ext == L".mdown" || ext == L".mkd";
}

void WINAPI SetStartupInfoW(const PluginStartupInfo* Info) {
    g_info = *Info;
    g_fsf = Info->FSF;
}

void WINAPI GetGlobalInfoW(GlobalInfo* Info) {
    Info->StructSize = sizeof(GlobalInfo);
    Info->MinFarVersion.Major = 3;
    Info->MinFarVersion.Minor = 0;
    Info->MinFarVersion.Revision = 0;
    Info->MinFarVersion.Build = 5000;
    Info->MinFarVersion.Stage = VS_RELEASE;
    Info->Version.Major = 1;
    Info->Version.Minor = 0;
    Info->Version.Revision = 0;
    Info->Version.Build = 1;
    Info->Version.Stage = VS_RELEASE;
    Info->Guid = MainGuid;
    Info->Title = L"Markdown Viewer";
    Info->Description = L"View Markdown files";
    Info->Author = L"Far Plugins";
}

int WINAPI GetMinFarVersionW(void) {
    return MAKEFARVERSION(3, 0, 0, 5000, VS_RELEASE);
}

void WINAPI GetPluginInfoW(PluginInfo* Info) {
    Info->StructSize = sizeof(PluginInfo);
    Info->Flags = 0;

    static const GUID menuGuid = MenuGuid;
    static const wchar_t* menuStrings[] = { L"Markdown Viewer" };

    Info->PluginMenu.Guids = &menuGuid;
    Info->PluginMenu.Strings = menuStrings;
    Info->PluginMenu.Count = 1;
}

HANDLE WINAPI OpenW(const OpenInfo* Info) {
    std::wstring path;

    if (Info->OpenFrom == OPEN_COMMANDLINE) {
        auto* cli = reinterpret_cast<const OpenCommandLineInfo*>(Info->Data);
        if (cli && cli->CommandLine)
            path = cli->CommandLine;
    } else {
        path = GetCurrentFilePath();
    }

    if (path.empty() || !IsMarkdownFile(path)) {
        const wchar_t* msg[] = {
            L"Markdown Viewer",
            L"",
            L"Please select a Markdown file (.md, .markdown, .mdown, .mkd)",
        };
        g_info.Message(MainGuid, MenuGuid, FMSG_MB_OK, nullptr, msg, 3, 1);
        return nullptr;
    }

    std::vector<std::wstring> lines;
    ReadFileLines(path, lines);

    if (lines.empty()) {
        const wchar_t* msg[] = {
            L"Markdown Viewer",
            L"",
            L"File is empty or cannot be read",
        };
        g_info.Message(MainGuid, MenuGuid, FMSG_MB_OK, nullptr, msg, 3, 1);
        return nullptr;
    }

    // Simple display - show first 20 lines
    std::wstring title = L"Markdown Viewer - " + path;
    std::vector<const wchar_t*> msg;
    msg.push_back(title.c_str());
    msg.push_back(L"");
    for (size_t i = 0; i < lines.size() && i < 20; ++i) {
        msg.push_back(lines[i].c_str());
    }

    g_info.Message(MainGuid, MenuGuid, FMSG_MB_OK, nullptr, msg.data(), msg.size(), 1);

    return nullptr;
}

intptr_t WINAPI ConfigureW(const ConfigureInfo*) {
    const wchar_t* msg[] = {
        L"Markdown Viewer",
        L"",
        L"Version 1.0",
    };
    g_info.Message(MainGuid, MenuGuid, FMSG_MB_OK, nullptr, msg, 3, 1);
    return 0;
}

void WINAPI ExitFARW(const ExitInfo*) {
}
