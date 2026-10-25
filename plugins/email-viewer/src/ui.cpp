#include "ui.hpp"
#include "html_converter.hpp"
#include "guid.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>

static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring result(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &result[0], needed);
    return result;
}

static int HexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static std::string Base64Decode(std::string text) {
    for (char& ch : text)
        if (ch == ',') ch = '/';
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int value = 0;
    int bits = 0;
    for (unsigned char ch : text) {
        if (ch == '=' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') continue;
        const char* found = std::strchr(alphabet, ch);
        if (!found) continue;
        value = (value << 6) | static_cast<int>(found - alphabet);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((value >> bits) & 0xFF));
        }
    }
    return out;
}

static UINT CodePageOf(std::string charset) {
    for (char& ch : charset) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (charset.find("1251") != std::string::npos) return 1251;
    if (charset.find("koi8") != std::string::npos) return 20866;
    if (charset.find("866") != std::string::npos) return 866;
    if (charset.find("8859-1") != std::string::npos || charset.find("latin1") != std::string::npos) return 28591;
    return CP_UTF8;
}

static std::wstring BytesToWide(const std::string& bytes, const std::string& charset) {
    if (bytes.empty()) return {};
    const UINT codePage = CodePageOf(charset);
    const int chars = MultiByteToWideChar(codePage, 0, bytes.c_str(), static_cast<int>(bytes.size()), nullptr, 0);
    if (chars <= 0) return utf8ToWide(bytes);
    std::wstring out(chars, L'\0');
    MultiByteToWideChar(codePage, 0, bytes.c_str(), static_cast<int>(bytes.size()), out.data(), chars);
    return out;
}

static std::wstring DecodeHeader(const std::string& text) {
    std::wstring out;
    for (size_t i = 0; i < text.size();) {
        if (text[i] != '=' || i + 1 >= text.size() || text[i + 1] != '?') {
            out.push_back(static_cast<unsigned char>(text[i]));
            ++i;
            continue;
        }
        const size_t charsetEnd = text.find('?', i + 2);
        const size_t methodEnd = charsetEnd == std::string::npos ? std::string::npos : text.find('?', charsetEnd + 1);
        const size_t dataEnd = methodEnd == std::string::npos ? std::string::npos : text.find("?=", methodEnd + 1);
        if (charsetEnd == std::string::npos || methodEnd == std::string::npos || dataEnd == std::string::npos) {
            out.push_back(static_cast<unsigned char>(text[i]));
            ++i;
            continue;
        }
        const std::string charset = text.substr(i + 2, charsetEnd - (i + 2));
        const char method = static_cast<char>(std::toupper(static_cast<unsigned char>(text[charsetEnd + 1])));
        std::string data = text.substr(methodEnd + 1, dataEnd - (methodEnd + 1));
        std::string bytes;
        if (method == 'B') {
            bytes = Base64Decode(data);
        } else {
            for (size_t j = 0; j < data.size(); ++j) {
                if (data[j] == '_') {
                    bytes.push_back(' ');
                } else if (data[j] == '=' && j + 2 < data.size() && HexValue(data[j + 1]) >= 0 && HexValue(data[j + 2]) >= 0) {
                    bytes.push_back(static_cast<char>((HexValue(data[j + 1]) << 4) | HexValue(data[j + 2])));
                    j += 2;
                } else {
                    bytes.push_back(data[j]);
                }
            }
        }
        out += BytesToWide(bytes, charset);
        i = dataEnd + 2;
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
            size_t look = i;
            while (look < text.size() && (text[look] == ' ' || text[look] == '\t')) ++look;
            if (look + 1 < text.size() && text[look] == '=' && text[look + 1] == '?') i = look;
            else break;
        }
    }
    return out;
}

static std::wstring DecodeMailboxName(const std::string& name) {
    std::wstring out;
    for (size_t i = 0; i < name.size();) {
        if (name[i] != '&') {
            out.push_back(static_cast<unsigned char>(name[i]));
            ++i;
            continue;
        }
        if (i + 1 < name.size() && name[i + 1] == '-') {
            out.push_back(L'&');
            i += 2;
            continue;
        }
        const size_t end = name.find('-', i + 1);
        if (end == std::string::npos) {
            out.push_back(static_cast<unsigned char>(name[i]));
            ++i;
            continue;
        }
        const std::string bytes = Base64Decode(name.substr(i + 1, end - i - 1));
        for (size_t j = 0; j + 1 < bytes.size(); j += 2)
            out.push_back(static_cast<wchar_t>((static_cast<unsigned char>(bytes[j]) << 8) | static_cast<unsigned char>(bytes[j + 1])));
        i = end + 1;
    }
    return out;
}

static std::wstring Person(const std::string& header) {
    std::wstring text = DecodeHeader(header);
    const auto bracket = text.find(L'<');
    if (bracket != std::wstring::npos && bracket > 1) text.resize(bracket);
    while (!text.empty() && (text.back() == L' ' || text.back() == L'"')) text.pop_back();
    while (!text.empty() && (text.front() == L' ' || text.front() == L'"')) text.erase(text.begin());
    return text.empty() ? L"(нет адреса)" : text;
}

static void Note(const PluginStartupInfo& info, const wchar_t* text) {
    const wchar_t* items[] = { L"Почта", text };
    if (info.Message) info.Message(MainGuid, ErrorGuid, FMSG_MB_OK | FMSG_LEFTALIGN, nullptr, items, 2, 1);
}

static intptr_t Pick(const PluginStartupInfo& info, const GUID& guid, const wchar_t* title, const wchar_t* bottom, const std::vector<std::wstring>& lines, intptr_t selected = 0) {
    if (!info.Menu || lines.empty()) return -1;
    std::vector<FarMenuItem> items(lines.size());
    for (size_t i = 0; i < lines.size(); ++i) items[i].Text = lines[i].c_str();
    if (selected >= 0 && static_cast<size_t>(selected) < items.size())
        items[static_cast<size_t>(selected)].Flags = 0x10000; // MIF_SELECTED
    return info.Menu(MainGuid, guid, -1, -1, 0, 0x2, title, bottom, nullptr, nullptr, nullptr, items.data(), items.size());
}

static const PluginStartupInfo* g_settingsInfo = nullptr;
static ULONGLONG g_ignoreEnterUntil = 0;

static bool IsEnterDown(const void* param2) {
    if (!param2) return false;
    const auto* rec = static_cast<const INPUT_RECORD*>(param2);
    return rec->EventType == KEY_EVENT && rec->Event.KeyEvent.bKeyDown &&
           rec->Event.KeyEvent.wVirtualKeyCode == VK_RETURN;
}

static bool HostFilled(HANDLE dlg) {
    if (!g_settingsInfo || !g_settingsInfo->SendDlgMessage) return true;
    const auto* host = reinterpret_cast<const wchar_t*>(
        g_settingsInfo->SendDlgMessage(dlg, DM_GETCONSTTEXTPTR, 4, nullptr));
    if (!host) return false;
    while (*host == L' ' || *host == L'\t') ++host;
    return *host != L'\0';
}

static intptr_t WINAPI SettingsDlgProc(HANDLE dlg, intptr_t msg, intptr_t param1, void* param2) {
    if (msg == DN_INITDIALOG) {
        g_ignoreEnterUntil = GetTickCount64() + 500;
        if (g_settingsInfo && g_settingsInfo->SendDlgMessage)
            g_settingsInfo->SendDlgMessage(dlg, DM_SETINPUTNOTIFY, 1, nullptr);
    }
    const bool opening = GetTickCount64() < g_ignoreEnterUntil;
    if (opening && (msg == DN_INPUT || msg == DN_CONTROLINPUT) && IsEnterDown(param2))
        return msg == DN_INPUT ? 0 : 1;
    if (msg == DN_CLOSE && param1 == 12 && (opening || !HostFilled(dlg))) {
        if (!opening && g_settingsInfo && g_settingsInfo->SendDlgMessage) {
            const wchar_t* title = L" Укажите сервер ";
            FarDialogItemData data{};
            data.StructSize = sizeof(data);
            data.PtrLength = static_cast<intptr_t>(wcslen(title));
            data.PtrData = const_cast<wchar_t*>(title);
            g_settingsInfo->SendDlgMessage(dlg, DM_SETTEXT, 0, &data);
            g_settingsInfo->SendDlgMessage(dlg, DM_SETFOCUS, 4, nullptr);
        }
        return 0;
    }
    if (g_settingsInfo && g_settingsInfo->DefDlgProc)
        return g_settingsInfo->DefDlgProc(dlg, msg, param1, param2);
    return 1;
}

bool ShowSettingsDialog(const PluginStartupInfo& info, ImapAccount& account) {
    wchar_t name[256]{};
    wchar_t host[256]{};
    wchar_t port[16]{};
    wchar_t login[256]{};
    wchar_t password[256]{};
    wcsncpy_s(name, account.name.c_str(), _TRUNCATE);
    wcsncpy_s(host, account.settings.host.c_str(), _TRUNCATE);
    wcsncpy_s(port, std::to_wstring(account.settings.port ? account.settings.port : 993).c_str(), _TRUNCATE);
    wcsncpy_s(login, account.settings.login.c_str(), _TRUNCATE);
    wcsncpy_s(password, account.settings.password.c_str(), _TRUNCATE);

    FarDialogItem items[14]{};
    items[0].Type = DI_DOUBLEBOX;
    items[0].X1 = 0;
    items[0].Y1 = 0;
    items[0].X2 = 59;
    items[0].Y2 = 16;
    items[0].Data = L" Почта ";

    const wchar_t* labels[] = { L"Имя:", L"Сервер:", L"Порт:", L"Логин:", L"Пароль:" };
    wchar_t* buffers[] = { name, host, port, login, password };
    const int kinds[] = { DI_EDIT, DI_EDIT, DI_EDIT, DI_EDIT, DI_PSWEDIT };
    const intptr_t lengths[] = { 255, 255, 15, 255, 255 };
    for (int i = 0; i < 5; ++i) {
        items[1 + i * 2].Type = DI_TEXT;
        items[1 + i * 2].X1 = 2;
        items[1 + i * 2].Y1 = 2 + i * 2;
        items[1 + i * 2].Data = labels[i];
        items[2 + i * 2].Type = kinds[i];
        items[2 + i * 2].X1 = 14;
        items[2 + i * 2].Y1 = 2 + i * 2;
        items[2 + i * 2].X2 = 56;
        items[2 + i * 2].Data = buffers[i];
        items[2 + i * 2].MaxLength = lengths[i];
        if (i == 0) items[2 + i * 2].Flags = DIF_FOCUS;
    }

    items[11].Type = DI_CHECKBOX;
    items[11].X1 = 2;
    items[11].Y1 = 12;
    items[11].Param.Selected = account.settings.useSSL ? 1 : 0;
    items[11].Data = L"SSL";

    items[12].Type = DI_BUTTON;
    items[12].X1 = 16;
    items[12].Y1 = 14;
    items[12].Flags = DIF_DEFAULTBUTTON | DIF_CENTERGROUP;
    items[12].Data = L"Сохранить";

    items[13].Type = DI_BUTTON;
    items[13].X1 = 28;
    items[13].Y1 = 14;
    items[13].Flags = DIF_CENTERGROUP;
    items[13].Data = L"Отмена";

    g_settingsInfo = &info;
    g_ignoreEnterUntil = GetTickCount64() + 500;
    HANDLE dialog = info.DialogInit(
        MainGuid, SettingsDialogGuid, -1, -1, 60, 17, nullptr, items, 14, 0, 0, SettingsDlgProc, 0);
    if (!dialog || dialog == INVALID_HANDLE_VALUE) {
        g_settingsInfo = nullptr;
        return false;
    }

    const intptr_t result = info.DialogRun(dialog);
    g_settingsInfo = nullptr;
    const bool saved = result == 12;
    if (saved && info.SendDlgMessage) {
        auto text = [&](int id) -> std::wstring {
            const wchar_t* value = reinterpret_cast<const wchar_t*>(
                info.SendDlgMessage(dialog, DM_GETCONSTTEXTPTR, id, nullptr));
            return value ? value : L"";
        };
        account.name = text(2);
        account.settings.host = text(4);
        account.settings.login = text(8);
        account.settings.password = text(10);
        account.settings.useSSL = info.SendDlgMessage(dialog, DM_GETCHECK, 11, nullptr) == 1;
        try {
            account.settings.port = std::stoi(text(6));
        } catch (...) {
            account.settings.port = account.settings.useSSL ? 993 : 143;
        }
        if (account.settings.port <= 0) account.settings.port = account.settings.useSSL ? 993 : 143;
        if (account.name.empty())
            account.name = account.settings.login.empty() ? account.settings.host : account.settings.login;
    }
    info.DialogFree(dialog);
    return saved && !account.settings.host.empty();
}

void ConfigureAccounts(const PluginStartupInfo& info, std::vector<ImapAccount>& accounts) {
    intptr_t selected = static_cast<intptr_t>(accounts.size());
    for (;;) {
        std::vector<std::wstring> lines;
        lines.reserve(accounts.size() + 1);
        for (const auto& account : accounts)
            lines.push_back(account.name.empty() ? account.settings.host : account.name);
        lines.push_back(L"Добавить учётную запись");
        if (selected < 0 || static_cast<size_t>(selected) >= lines.size())
            selected = static_cast<intptr_t>(lines.size() - 1);
        const intptr_t pick = Pick(info, FolderListDialogGuid, L"Учётные записи", L"Enter — изменить или добавить, Esc — выход", lines, selected);
        if (pick < 0) return;
        selected = pick;
        if (pick == static_cast<intptr_t>(accounts.size())) {
            ImapAccount account;
            account.settings.port = 993;
            account.settings.useSSL = true;
            if (ShowSettingsDialog(info, account)) accounts.push_back(std::move(account));
            continue;
        }
        ShowSettingsDialog(info, accounts[static_cast<size_t>(pick)]);
    }
}

void ShowAccountList(const PluginStartupInfo& info, const std::vector<ImapAccount>& accounts, int& selectedIndex) {
    selectedIndex = -1;
    std::vector<std::wstring> lines;
    lines.reserve(accounts.size());
    for (const auto& account : accounts) lines.push_back(account.name);
    const intptr_t pick = Pick(info, FolderListDialogGuid, L"Учётные записи", L"Enter — выбрать, Esc — назад", lines);
    if (pick >= 0 && pick < static_cast<intptr_t>(accounts.size())) selectedIndex = static_cast<int>(pick);
}

void ShowFolderList(const PluginStartupInfo& info, ImapClient& client, std::string& selectedFolder) {
    selectedFolder.clear();
    const auto folders = client.listFolders();
    if (folders.empty()) {
        Note(info, L"На сервере нет папок.");
        return;
    }
    struct ShownFolder {
        std::wstring title;
        std::string name;
    };
    std::vector<ShownFolder> shown;
    shown.reserve(folders.size());
    for (const auto& folder : folders)
        shown.push_back({ DecodeMailboxName(folder.name), folder.name });
    std::sort(shown.begin(), shown.end(), [](const ShownFolder& a, const ShownFolder& b) {
        const bool aInbox = _stricmp(a.name.c_str(), "INBOX") == 0;
        const bool bInbox = _stricmp(b.name.c_str(), "INBOX") == 0;
        if (aInbox != bInbox) return aInbox;
        return _wcsicmp(a.title.c_str(), b.title.c_str()) < 0;
    });
    std::vector<std::wstring> lines;
    lines.reserve(shown.size());
    for (const auto& folder : shown) lines.push_back(folder.title);
    const intptr_t pick = Pick(info, FolderListDialogGuid, L"Папки", L"Enter — открыть, Esc — назад", lines);
    if (pick >= 0 && pick < static_cast<intptr_t>(shown.size())) selectedFolder = shown[pick].name;
}

static std::wstring Fit(std::wstring text, size_t width) {
    if (text.size() > width) text.resize(width);
    while (text.size() < width) text.push_back(L' ');
    return text;
}

void ShowMessageList(const PluginStartupInfo& info, ImapClient& client, const std::string& folder) {
    const auto messages = client.listMessages(folder);
    if (messages.empty()) {
        Note(info, L"В папке нет писем.");
        return;
    }
    const std::wstring title = DecodeMailboxName(folder);
    std::vector<std::wstring> lines;
    lines.reserve(messages.size());
    for (const auto& message : messages) {
        std::wstring date = utf8ToWide(message.date);
        if (date.size() > 11) date.resize(11);
        const std::wstring subject = DecodeHeader(message.subject.empty() ? "(без темы)" : message.subject);
        lines.push_back(Fit(date, 12) + Fit(Person(message.from), 24) + subject);
    }
    while (true) {
        const intptr_t pick = Pick(info, MessageListDialogGuid, title.c_str(), L"Enter — читать, Esc — к папкам", lines);
        if (pick < 0 || pick >= static_cast<intptr_t>(messages.size())) return;
        ShowMessageView(info, client.fetchMessage(folder, messages[pick].uid));
    }
}

static std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(bytes > 0 ? bytes : 0, '\0');
    if (bytes > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

static bool HasVisibleText(const std::string& text) {
    return std::any_of(text.begin(), text.end(), [](unsigned char ch) { return ch > 32; });
}

void ShowMessageView(const PluginStartupInfo& info, const ImapMessageBody& msg) {
    std::string plain = msg.textBody;
    if (plain.find('<') != std::string::npos && plain.find('>') != std::string::npos)
        plain = htmlToPlainText(plain);
    if (!HasVisibleText(plain))
        plain = htmlToPlainText(msg.htmlBody);

    const std::wstring subject = DecodeHeader(msg.subject.empty() ? "(без темы)" : msg.subject);
    std::wstring text;
    text += L"От: " + Person(msg.from) + L"\r\n";
    text += L"Кому: " + DecodeHeader(msg.to) + L"\r\n";
    text += L"Тема: " + subject + L"\r\n";
    text += L"Дата: " + utf8ToWide(msg.date) + L"\r\n";
    text += L"\r\n";
    text += utf8ToWide(plain);
    text.erase(std::remove(text.begin(), text.end(), L'\0'), text.end());
    std::wstring normalized;
    normalized.reserve(text.size());
    for (wchar_t ch : text) {
        if (ch == L'\r') continue;
        if (ch == L'\n') normalized += L"\r\n";
        else normalized.push_back(ch);
    }

    wchar_t directory[MAX_PATH]{};
    wchar_t path[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"eml", 0, path)) {
        Note(info, L"Не удалось открыть письмо.");
        return;
    }
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"wb") != 0 || !file) {
        Note(info, L"Не удалось открыть письмо.");
        return;
    }
    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    const std::string utf8 = WideToUtf8(normalized);
    fwrite(bom, 1, sizeof(bom), file);
    fwrite(utf8.data(), 1, utf8.size(), file);
    fclose(file);

    if (info.Viewer)
        info.Viewer(path, subject.c_str(), -1, -1, -1, -1, 0x200 | 0x8, 65001);
    else
        DeleteFileW(path);
}
