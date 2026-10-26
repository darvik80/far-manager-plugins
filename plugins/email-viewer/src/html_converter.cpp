#include "html_converter.hpp"

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <string>

namespace {

bool StartsWith(const std::string& text, size_t pos, const char* literal) {
    for (size_t i = 0; literal[i]; ++i) {
        if (pos + i >= text.size()) return false;
        const auto have = static_cast<unsigned char>(text[pos + i]);
        const auto want = static_cast<unsigned char>(literal[i]);
        if (std::tolower(have) != std::tolower(want)) return false;
    }
    return true;
}

bool Equals(const std::string& name, const char* literal) {
    return StartsWith(name, 0, literal) && name.size() == std::char_traits<char>::length(literal);
}

std::string TagName(const std::string& tag) {
    size_t i = 0;
    if (i < tag.size() && tag[i] == '/') ++i;
    std::string name;
    while (i < tag.size()) {
        const auto ch = static_cast<unsigned char>(tag[i]);
        if (!std::isalnum(ch)) break;
        name.push_back(static_cast<char>(std::tolower(ch)));
        ++i;
    }
    return name;
}

size_t FindTagEnd(const std::string& html, size_t open) {
    bool quoted = false;
    char quote = 0;
    for (size_t i = open + 1; i < html.size(); ++i) {
        const char ch = html[i];
        if (quoted) {
            if (ch == quote) quoted = false;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quoted = true;
            quote = ch;
            continue;
        }
        if (ch == '>') return i;
    }
    return std::string::npos;
}

size_t SkipBlock(const std::string& html, size_t from, const char* closeTag) {
    const size_t closeLen = std::char_traits<char>::length(closeTag);
    for (size_t i = from; i + closeLen <= html.size(); ++i) {
        if (!StartsWith(html, i, closeTag)) continue;
        const auto end = FindTagEnd(html, i);
        return end == std::string::npos ? html.size() : end + 1;
    }
    return from;
}

void AppendEntity(std::string& out, const std::string& html, size_t& i) {
    const auto semi = html.find(';', i + 1);
    if (semi == std::string::npos || semi - i > 12) {
        out.push_back(html[i]);
        ++i;
        return;
    }
    const std::string token = html.substr(i + 1, semi - i - 1);
    std::string lower = token;
    for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const char* plain = nullptr;
    if (lower == "amp") plain = "&";
    else if (lower == "lt") plain = "<";
    else if (lower == "gt") plain = ">";
    else if (lower == "quot") plain = "\"";
    else if (lower == "apos") plain = "'";
    else if (lower == "nbsp") plain = " ";
    else if (lower == "mdash" || lower == "ndash") plain = "-";
    else if (lower == "laquo") plain = "\"";
    else if (lower == "raquo") plain = "\"";
    else if (lower == "hellip") plain = "...";
    else if (lower == "bull") plain = "*";
    if (plain) {
        out += plain;
        i = semi + 1;
        return;
    }
    int code = -1;
    if (!token.empty() && token[0] == '#') {
        try {
            code = token[1] == 'x' || token[1] == 'X'
                ? std::stoi(token.substr(2), nullptr, 16)
                : std::stoi(token.substr(1));
        } catch (...) {
            code = -1;
        }
    }
    if (code == 160) {
        out.push_back(' ');
        i = semi + 1;
        return;
    }
    if (code > 0 && code < 0x110000) {
        wchar_t wide[2] = { static_cast<wchar_t>(code), 0 };
        if (code > 0xFFFF) {
            code -= 0x10000;
            wide[0] = static_cast<wchar_t>(0xD800 + (code >> 10));
            wide[1] = static_cast<wchar_t>(0xDC00 + (code & 0x3FF));
        }
        char utf8[8]{};
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, wide[1] ? 2 : 1, utf8, sizeof(utf8), nullptr, nullptr);
        if (bytes > 0) out.append(utf8, bytes);
        i = semi + 1;
        return;
    }
    out.push_back(html[i]);
    ++i;
}

bool IsBlock(const std::string& name) {
    return Equals(name, "p") || Equals(name, "br") || Equals(name, "hr") || Equals(name, "blockquote") ||
           Equals(name, "h1") || Equals(name, "h2") || Equals(name, "h3") || Equals(name, "h4") ||
           Equals(name, "h5") || Equals(name, "h6");
}

bool IsLayout(const std::string& name) {
    return Equals(name, "div") || Equals(name, "td") || Equals(name, "th") || Equals(name, "tr") ||
           Equals(name, "table") || Equals(name, "section") || Equals(name, "article") ||
           Equals(name, "header") || Equals(name, "footer") || Equals(name, "ul") || Equals(name, "ol");
}

void BreakLine(std::string& out) {
    if (!out.empty() && out.back() != '\n') out.push_back('\n');
}

uint32_t NextCode(const std::string& text, size_t& i) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c < 0x80) {
        ++i;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
        const uint32_t cp = (static_cast<uint32_t>(c & 0x1F) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
        i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
        const uint32_t cp = (static_cast<uint32_t>(c & 0x0F) << 12) |
                            (static_cast<uint32_t>(static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                            (static_cast<unsigned char>(text[i + 2]) & 0x3F);
        i += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
        const uint32_t cp = (static_cast<uint32_t>(c & 0x07) << 18) |
                            (static_cast<uint32_t>(static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
                            (static_cast<uint32_t>(static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6) |
                            (static_cast<unsigned char>(text[i + 3]) & 0x3F);
        i += 4;
        return cp;
    }
    ++i;
    return c;
}

void AppendCode(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
        return;
    }
    wchar_t wide[2] = { static_cast<wchar_t>(cp), 0 };
    int count = 1;
    if (cp > 0xFFFF) {
        cp -= 0x10000;
        wide[0] = static_cast<wchar_t>(0xD800 + (cp >> 10));
        wide[1] = static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        count = 2;
    }
    char utf8[8]{};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, count, utf8, sizeof(utf8), nullptr, nullptr);
    if (bytes > 0) out.append(utf8, bytes);
}

bool IsSpace(uint32_t cp) {
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0xA0 || cp == 0xAD || cp == 0xFEFF)
        return true;
    if (cp >= 0x2000 && cp <= 0x200D) return true;
    return cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

bool IsBreak(uint32_t cp) {
    return cp == '\n' || cp == '\r' || cp == 0x2028 || cp == 0x2029;
}

std::string Collapse(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    std::string line;
    bool breakBefore = false;
    auto flush = [&]() {
        size_t begin = 0;
        size_t at = 0;
        while (at < line.size()) {
            const size_t mark = at;
            if (!IsSpace(NextCode(line, at))) {
                begin = mark;
                break;
            }
            begin = at;
        }
        size_t end = begin;
        at = begin;
        while (at < line.size()) {
            const size_t mark = at;
            if (IsSpace(NextCode(line, at))) continue;
            end = at;
            (void)mark;
        }
        if (begin >= line.size() || begin >= end) {
            line.clear();
            return;
        }
        if (breakBefore && !out.empty()) out.push_back('\n');
        breakBefore = false;
        out.append(line, begin, end - begin);
        line.clear();
    };
    for (size_t i = 0; i < text.size();) {
        const uint32_t cp = NextCode(text, i);
        if (IsBreak(cp)) {
            flush();
            breakBefore = true;
            continue;
        }
        if (IsSpace(cp)) {
            if (!line.empty() && line.back() != ' ') line.push_back(' ');
            continue;
        }
        AppendCode(line, cp);
    }
    flush();
    return out;
}

}  // namespace

std::string htmlToPlainText(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    for (size_t i = 0; i < html.size();) {
        if (html[i] != '<') {
            if (html[i] == '&') AppendEntity(out, html, i);
            else {
                out.push_back(html[i]);
                ++i;
            }
            continue;
        }
        if (StartsWith(html, i, "<!--")) {
            const auto end = html.find("-->", i + 4);
            i = end == std::string::npos ? html.size() : end + 3;
            continue;
        }
        const auto end = FindTagEnd(html, i);
        if (end == std::string::npos) break;
        const std::string tag = html.substr(i + 1, end - i - 1);
        const std::string name = TagName(tag);
        const bool closing = !tag.empty() && tag[0] == '/';
        i = end + 1;
        if (!closing && (Equals(name, "script") || Equals(name, "style") || Equals(name, "head"))) {
            const std::string close = "</" + name;
            const size_t next = SkipBlock(html, i, close.c_str());
            if (next != i) i = next;
            continue;
        }
        if (Equals(name, "li") && !closing) {
            BreakLine(out);
            out += "* ";
            continue;
        }
        if (IsBlock(name)) BreakLine(out);
        else if (closing && IsLayout(name) && !out.empty() && out.back() != ' ' && out.back() != '\n')
            out.push_back(' ');
    }
    return Collapse(out);
}

std::string normalizeMailText(const std::string& text) {
    return Collapse(text);
}

size_t visibleMailCount(const std::string& text) {
    size_t count = 0;
    for (size_t i = 0; i < text.size();) {
        const uint32_t cp = NextCode(text, i);
        if (!IsSpace(cp)) ++count;
    }
    return count;
}
