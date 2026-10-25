#include "far/plugin.hpp"
#include "guid.hpp"

#include <string>
#include <vector>

static PluginStartupInfo g_info{};

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
    return dir + L"\\" + pItem->FileName;
}

void WINAPI SetStartupInfoW(const PluginStartupInfo* Info) {
    g_info = *Info;
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
    std::wstring path = GetCurrentFilePath();
    std::wstring status = path.empty() ? L"No file selected" : path;

    std::vector<const wchar_t*> msg;
    std::vector<std::wstring> storage;
    storage.push_back(L"Markdown Viewer");
    storage.push_back(L"");
    storage.push_back(L"Plugin loaded successfully!");
    storage.push_back(L"");
    storage.push_back(status);

    for (const auto& s : storage) {
        msg.push_back(s.c_str());
    }

    g_info.Message(
        MainGuid,
        MenuGuid,
        FMSG_MB_OK,
        nullptr,
        msg.data(),
        msg.size(),
        1
    );

    return nullptr;
}

intptr_t WINAPI ConfigureW(const ConfigureInfo*) {
    return 0;
}

void WINAPI ExitFARW(const ExitInfo*) {
}
