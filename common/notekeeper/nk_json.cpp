#include "nk_json.hpp"
#include "nk_text.hpp"

#include <cctype>
#include <cstdlib>

namespace {

void SkipWs(const std::string& text, size_t& i) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
}

bool ParseJsonString(const std::string& text, size_t& i, std::wstring& out) {
    SkipWs(text, i);
    if (i >= text.size() || text[i] != '"') return false;
    ++i;
    std::string raw;
    while (i < text.size()) {
        const char ch = text[i++];
        if (ch == '"') {
            out = NkWide(raw);
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
        case 'b': raw.push_back('\b'); break;
        case 'f': raw.push_back('\f'); break;
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

bool ExpectColon(const std::string& text, size_t& i) {
    SkipWs(text, i);
    if (i >= text.size() || text[i] != ':') return false;
    ++i;
    return true;
}

bool SkipJson(const std::string& text, size_t& i);

bool ParseStringArray(const std::string& text, size_t& i, std::vector<std::wstring>& out) {
    SkipWs(text, i);
    if (i >= text.size() || text[i] != '[') return false;
    ++i;
    SkipWs(text, i);
    if (i < text.size() && text[i] == ']') {
        ++i;
        return true;
    }
    for (;;) {
        std::wstring value;
        if (!ParseJsonString(text, i, value)) return false;
        out.push_back(std::move(value));
        SkipWs(text, i);
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        SkipWs(text, i);
        if (i < text.size() && text[i] == ']') {
            ++i;
            return true;
        }
        return false;
    }
}

bool SkipJson(const std::string& text, size_t& i) {
    SkipWs(text, i);
    if (i >= text.size()) return false;
    if (text[i] == '"') {
        std::wstring dummy;
        return ParseJsonString(text, i, dummy);
    }
    if (text[i] == '{') {
        ++i;
        SkipWs(text, i);
        if (i < text.size() && text[i] == '}') {
            ++i;
            return true;
        }
        for (;;) {
            std::wstring key;
            if (!ParseJsonString(text, i, key) || !ExpectColon(text, i) || !SkipJson(text, i)) return false;
            SkipWs(text, i);
            if (i < text.size() && text[i] == ',') {
                ++i;
                continue;
            }
            SkipWs(text, i);
            if (i < text.size() && text[i] == '}') {
                ++i;
                return true;
            }
            return false;
        }
    }
    if (text[i] == '[') {
        ++i;
        SkipWs(text, i);
        if (i < text.size() && text[i] == ']') {
            ++i;
            return true;
        }
        for (;;) {
            if (!SkipJson(text, i)) return false;
            SkipWs(text, i);
            if (i < text.size() && text[i] == ',') {
                ++i;
                continue;
            }
            SkipWs(text, i);
            if (i < text.size() && text[i] == ']') {
                ++i;
                return true;
            }
            return false;
        }
    }
    if (text.compare(i, 4, "null") == 0) { i += 4; return true; }
    if (text.compare(i, 4, "true") == 0) { i += 4; return true; }
    if (text.compare(i, 5, "false") == 0) { i += 5; return true; }
    if (text[i] == '-' || std::isdigit(static_cast<unsigned char>(text[i]))) {
        if (text[i] == '-') ++i;
        while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.' ||
               text[i] == 'e' || text[i] == 'E' || text[i] == '+'))
            ++i;
        return true;
    }
    return false;
}

bool ParseNumber(const std::string& text, size_t& i, int& value) {
    SkipWs(text, i);
    if (i >= text.size() || (!std::isdigit(static_cast<unsigned char>(text[i])) && text[i] != '-')) return false;
    size_t start = i;
    if (text[i] == '-') ++i;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
    value = atoi(text.substr(start, i - start).c_str());
    return true;
}

}  // namespace

bool NkParseObjectArrayAt(const std::string& json, size_t& i, std::vector<NkObject>& out, bool nested);

bool NkParseObject(const std::string& text, size_t& i, NkObject& out, bool nested) {
    out = {};
    SkipWs(text, i);
    if (i >= text.size() || text[i] != '{') return false;
    ++i;
    SkipWs(text, i);
    if (i < text.size() && text[i] == '}') {
        ++i;
        return true;
    }
    for (;;) {
        std::wstring keyWide;
        if (!ParseJsonString(text, i, keyWide) || !ExpectColon(text, i)) return false;
        const std::string key = NkUtf8(keyWide);
        SkipWs(text, i);
        if (i >= text.size()) return false;
        if (text[i] == '"') {
            std::wstring value;
            if (!ParseJsonString(text, i, value)) return false;
            out.str[key] = std::move(value);
        } else if (text.compare(i, 4, "true") == 0) {
            i += 4;
            out.boolean[key] = true;
        } else if (text.compare(i, 5, "false") == 0) {
            i += 5;
            out.boolean[key] = false;
        } else if (text.compare(i, 4, "null") == 0) {
            i += 4;
        } else if (text[i] == '[') {
            std::vector<std::wstring> values;
            const size_t mark = i;
            if (ParseStringArray(text, i, values)) {
                out.arrays[key] = std::move(values);
            } else if (nested) {
                i = mark;
                std::vector<NkObject> items;
                if (!NkParseObjectArrayAt(text, i, items, nested)) return false;
                out.objectArrays[key] = std::move(items);
            } else {
                i = mark;
                if (!SkipJson(text, i)) return false;
            }
        } else if (text[i] == '{') {
            if (nested) {
                NkObject child;
                if (!NkParseObject(text, i, child, nested)) return false;
                out.objects[key] = std::move(child);
            } else if (!SkipJson(text, i)) {
                return false;
            }
        } else if (text[i] == '-' || std::isdigit(static_cast<unsigned char>(text[i]))) {
            int number = 0;
            if (!ParseNumber(text, i, number)) return false;
            out.number[key] = number;
        } else if (!SkipJson(text, i)) {
            return false;
        }
        SkipWs(text, i);
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        SkipWs(text, i);
        if (i < text.size() && text[i] == '}') {
            ++i;
            return true;
        }
        return false;
    }
}

bool NkParseObjectArrayAt(const std::string& json, size_t& i, std::vector<NkObject>& out, bool nested) {
    out.clear();
    SkipWs(json, i);
    if (i >= json.size() || json[i] != '[') return false;
    ++i;
    SkipWs(json, i);
    if (i < json.size() && json[i] == ']') {
        ++i;
        return true;
    }
    for (;;) {
        NkObject item;
        if (!NkParseObject(json, i, item, nested)) return false;
        out.push_back(std::move(item));
        SkipWs(json, i);
        if (i < json.size() && json[i] == ',') {
            ++i;
            continue;
        }
        SkipWs(json, i);
        if (i < json.size() && json[i] == ']') {
            ++i;
            return true;
        }
        return false;
    }
}

bool NkParseObjectArray(const std::string& json, std::vector<NkObject>& out) {
    size_t i = 0;
    SkipWs(json, i);
    if (i < json.size() && json[i] == '[') return NkParseObjectArrayAt(json, i, out, true);
    if (i < json.size() && json[i] == '{') {
        NkObject root;
        if (!NkParseObject(json, i, root, true)) return false;
        static const char* keys[] = { "items", "notes", "todos", "data", "result", "content" };
        for (const char* key : keys) {
            auto found = root.objectArrays.find(key);
            if (found != root.objectArrays.end()) {
                out = found->second;
                return true;
            }
        }
    }
    out.clear();
    return false;
}

bool NkMcpPayload(const std::string& json, std::string& payload, std::wstring& error) {
    payload.clear();
    size_t i = 0;
    NkObject root;
    if (!NkParseObject(json, i, root, true)) {
        error = L"MCP вернул непонятный JSON.";
        return false;
    }
    auto err = root.objects.find("error");
    if (err != root.objects.end()) {
        error = err->second.S("message");
        if (error.empty()) error = L"MCP error";
        return false;
    }
    auto result = root.objects.find("result");
    if (result == root.objects.end()) {
        error = L"MCP: нет result.";
        return false;
    }
    auto content = result->second.objectArrays.find("content");
    if (content == result->second.objectArrays.end() || content->second.empty()) {
        error = L"MCP: нет content.";
        return false;
    }
    payload = NkUtf8(content->second[0].S("text"));
    if (payload.empty()) {
        error = L"MCP вернул пустой ответ.";
        return false;
    }
    return true;
}

std::string NkJsonEscape(const std::wstring& wide) {
    const std::string text = NkUtf8(wide);
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

std::string NkJsonStringArray(const std::vector<std::wstring>& values) {
    std::string out = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out += ',';
        out += '"';
        out += NkJsonEscape(values[i]);
        out += '"';
    }
    out += ']';
    return out;
}
