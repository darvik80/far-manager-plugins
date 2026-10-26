#include "vault_link.hpp"
#include "guid.hpp"
#include "ui.hpp"

#include <sstream>
#include <vector>

namespace {

constexpr wchar_t kVaultId[] = L"7A4E91C2-3B6F-4D18-A9E0-1C5D8B2F7043";

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

struct MacroExec {
    size_t StructSize;
    unsigned long long Flags;
    const wchar_t* SequenceText;
    size_t InCount;
    MacroValue* InValues;
    size_t OutCount;
    const MacroValue* OutValues;
};

std::wstring QuoteLua(const std::wstring& text) {
    std::wstring out = L"\"";
    for (wchar_t ch : text) {
        if (ch == L'\\' || ch == L'"') out += L'\\';
        out += ch;
    }
    out += L'"';
    return out;
}

}  // namespace

bool VaultPluginPresent() {
    static int cached = -1;
    if (cached >= 0) return cached != 0;
    cached = 0;
    if (!g_info.MacroControl) return false;
    MacroValue in{};
    in.Type = 2;
    in.String = kVaultId;
    MacroExec exec{};
    exec.StructSize = sizeof(exec);
    exec.SequenceText = L"local id = ...; return Plugin.Exist(id)";
    exec.InCount = 1;
    exec.InValues = &in;
    if (!g_info.MacroControl(MainGuid, 10, 0, &exec) || !exec.OutCount || !exec.OutValues)
        return false;
    const auto& out = exec.OutValues[0];
    if (out.Type == 4) cached = out.Boolean != 0 ? 1 : 0;
    else if (out.Type == 1) cached = out.Integer != 0 ? 1 : 0;
    return cached != 0;
}

std::wstring VaultCall(const std::wstring& verb, const std::wstring& kind, const std::wstring& name, const std::wstring& secret) {
    if (!g_info.MacroControl || !VaultPluginPresent()) return {};
    std::wstring code = L"return Plugin.SyncCall(" + QuoteLua(kVaultId);
    code += L", " + QuoteLua(verb);
    if (!kind.empty() || !name.empty() || verb == L"store" || verb == L"list" || verb == L"lookup")
        code += L", " + QuoteLua(kind);
    if (!name.empty() || verb == L"lookup" || verb == L"store")
        code += L", " + QuoteLua(name);
    if (verb == L"store")
        code += L", " + QuoteLua(secret);
    code += L")";
    MacroExec exec{};
    exec.StructSize = sizeof(exec);
    exec.SequenceText = code.c_str();
    if (!g_info.MacroControl(MainGuid, 10, 0, &exec) || !exec.OutCount || !exec.OutValues)
        return {};
    const auto& out = exec.OutValues[0];
    if (out.Type == 2 && out.String) return out.String;
    return {};
}

std::vector<std::wstring> VaultNames(bool keys) {
    const std::wstring blob = VaultCall(L"list", keys ? L"keys" : L"passwords", {});
    std::vector<std::wstring> names;
    std::wstring line;
    for (wchar_t ch : blob) {
        if (ch == L'\n' || ch == L'\r') {
            if (!line.empty()) names.push_back(line);
            line.clear();
        } else {
            line += ch;
        }
    }
    if (!line.empty()) names.push_back(line);
    return names;
}

bool VaultStorePassword(const std::wstring& name, const std::wstring& secret) {
    if (name.empty() || secret.empty()) return false;
    return !VaultCall(L"store", L"password", name, secret).empty();
}

bool VaultStoreKeyFile(const std::wstring& name, const std::wstring& path) {
    if (name.empty() || path.empty()) return false;
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) return false;
    std::string bytes;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), file)) > 0) bytes.append(buf, n);
    fclose(file);
    int chars = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (chars <= 0) return false;
    std::wstring pem(chars, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), pem.data(), chars);
    return !VaultCall(L"store", L"key", name, pem).empty();
}
