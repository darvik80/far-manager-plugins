#include "json.hpp"
#include "text.hpp"

#include <cctype>
#include <cstdio>
#include <cstdint>

namespace {

void SkipWs(const std::string& text, size_t& i) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
}

bool Expect(const std::string& text, size_t& i, char ch) {
    SkipWs(text, i);
    if (i >= text.size() || text[i] != ch) return false;
    ++i;
    return true;
}

std::string Escape(const std::wstring& wide) {
    const std::string text = Utf8(wide);
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char ch : text) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                char buf[8];
                sprintf_s(buf, "\\u%04x", ch);
                out += buf;
            } else {
                out.push_back(static_cast<char>(ch));
            }
        }
    }
    return out;
}

bool ParseString(const std::string& text, size_t& i, std::wstring& out) {
    SkipWs(text, i);
    if (i >= text.size() || text[i] != '"') return false;
    ++i;
    std::string raw;
    while (i < text.size()) {
        const char ch = text[i++];
        if (ch == '"') {
            out = Wide(raw);
            return true;
        }
        if (ch != '\\') {
            raw.push_back(ch);
            continue;
        }
        if (i >= text.size()) return false;
        const char next = text[i++];
        switch (next) {
        case '"':
        case '\\':
        case '/': raw.push_back(next); break;
        case 'n': raw.push_back('\n'); break;
        case 'r': raw.push_back('\r'); break;
        case 't': raw.push_back('\t'); break;
        case 'u': {
            if (i + 4 > text.size()) return false;
            unsigned code = 0;
            for (int n = 0; n < 4; ++n) {
                const char hex = text[i++];
                code <<= 4;
                if (hex >= '0' && hex <= '9') code |= hex - '0';
                else if (hex >= 'a' && hex <= 'f') code |= hex - 'a' + 10;
                else if (hex >= 'A' && hex <= 'F') code |= hex - 'A' + 10;
                else return false;
            }
            wchar_t wide[2] = { static_cast<wchar_t>(code), 0 };
            char utf8[8]{};
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, 1, utf8, sizeof(utf8), nullptr, nullptr);
            if (bytes > 0) raw.append(utf8, bytes);
            break;
        }
        default: return false;
        }
    }
    return false;
}

bool ParseObject(const std::string& text, size_t& i, std::vector<std::pair<std::string, std::wstring>>& fields) {
    if (!Expect(text, i, '{')) return false;
    SkipWs(text, i);
    if (i < text.size() && text[i] == '}') {
        ++i;
        return true;
    }
    for (;;) {
        std::wstring keyWide;
        if (!ParseString(text, i, keyWide)) return false;
        if (!Expect(text, i, ':')) return false;
        std::wstring value;
        if (!ParseString(text, i, value)) return false;
        fields.emplace_back(Utf8(keyWide), std::move(value));
        SkipWs(text, i);
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        return Expect(text, i, '}');
    }
}

template <typename Fn>
bool ParseArray(const std::string& text, size_t& i, Fn&& item) {
    if (!Expect(text, i, '[')) return false;
    SkipWs(text, i);
    if (i < text.size() && text[i] == ']') {
        ++i;
        return true;
    }
    for (;;) {
        if (!item()) return false;
        SkipWs(text, i);
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        return Expect(text, i, ']');
    }
}

const std::wstring* Field(const std::vector<std::pair<std::string, std::wstring>>& fields, const char* name) {
    for (const auto& field : fields)
        if (field.first == name) return &field.second;
    return nullptr;
}

std::wstring FieldOr(const std::vector<std::pair<std::string, std::wstring>>& fields, const char* name) {
    const auto* value = Field(fields, name);
    return value ? *value : std::wstring{};
}

}  // namespace

std::string VaultToJson(const VaultData& data) {
    std::string json = "{\n  \"passwords\": [\n";
    for (size_t i = 0; i < data.passwords.size(); ++i) {
        const auto& item = data.passwords[i];
        json += "    {\"id\":\"" + Escape(item.id) + "\",\"name\":\"" + Escape(item.name) +
                "\",\"user\":\"" + Escape(item.user) + "\",\"secret\":\"" + Escape(item.secret) +
                "\",\"host\":\"" + Escape(item.host) + "\",\"note\":\"" + Escape(item.note) + "\"}";
        json += (i + 1 < data.passwords.size()) ? ",\n" : "\n";
    }
    json += "  ],\n  \"keys\": [\n";
    for (size_t i = 0; i < data.keys.size(); ++i) {
        const auto& item = data.keys[i];
        json += "    {\"id\":\"" + Escape(item.id) + "\",\"name\":\"" + Escape(item.name) +
                "\",\"type\":\"" + Escape(item.type) + "\",\"comment\":\"" + Escape(item.comment) +
                "\",\"private\":\"" + Escape(item.privateKey) + "\",\"public\":\"" + Escape(item.publicKey) + "\"}";
        json += (i + 1 < data.keys.size()) ? ",\n" : "\n";
    }
    json += "  ]\n}\n";
    return json;
}

bool JsonToVault(const std::string& json, VaultData& data, std::wstring& error) {
    data = {};
    size_t i = 0;
    if (!Expect(json, i, '{')) {
        error = L"Хранилище повреждено.";
        return false;
    }
    while (i < json.size()) {
        SkipWs(json, i);
        if (i < json.size() && json[i] == '}') {
            ++i;
            return true;
        }
        std::wstring key;
        if (!ParseString(json, i, key) || !Expect(json, i, ':')) {
            error = L"Хранилище повреждено.";
            return false;
        }
        const std::string name = Utf8(key);
        if (name == "passwords") {
            if (!ParseArray(json, i, [&] {
                    std::vector<std::pair<std::string, std::wstring>> fields;
                    if (!ParseObject(json, i, fields)) return false;
                    PasswordRecord item;
                    item.id = FieldOr(fields, "id");
                    item.name = FieldOr(fields, "name");
                    item.user = FieldOr(fields, "user");
                    item.secret = FieldOr(fields, "secret");
                    item.host = FieldOr(fields, "host");
                    item.note = FieldOr(fields, "note");
                    if (!item.name.empty()) data.passwords.push_back(std::move(item));
                    return true;
                })) {
                error = L"Хранилище повреждено.";
                return false;
            }
        } else if (name == "keys") {
            if (!ParseArray(json, i, [&] {
                    std::vector<std::pair<std::string, std::wstring>> fields;
                    if (!ParseObject(json, i, fields)) return false;
                    KeyRecord item;
                    item.id = FieldOr(fields, "id");
                    item.name = FieldOr(fields, "name");
                    item.type = FieldOr(fields, "type");
                    item.comment = FieldOr(fields, "comment");
                    item.privateKey = FieldOr(fields, "private");
                    item.publicKey = FieldOr(fields, "public");
                    if (!item.name.empty()) data.keys.push_back(std::move(item));
                    return true;
                })) {
                error = L"Хранилище повреждено.";
                return false;
            }
        } else {
            error = L"Хранилище повреждено.";
            return false;
        }
        SkipWs(json, i);
        if (i < json.size() && json[i] == ',') {
            ++i;
            continue;
        }
        if (!Expect(json, i, '}')) {
            error = L"Хранилище повреждено.";
            return false;
        }
        return true;
    }
    error = L"Хранилище повреждено.";
    return false;
}
