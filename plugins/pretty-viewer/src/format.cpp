#include "format.hpp"

#include <cwctype>

namespace {

constexpr int kMaxDepth = 64;

std::vector<ViewLine> HighlightLoose(const std::wstring& text);

struct Out {
    std::vector<ViewLine> lines;
    ViewLine cur;

    void brk() {
        lines.push_back(std::move(cur));
        cur.spans.clear();
    }

    void add(Token token, std::wstring text) {
        if (!text.empty()) cur.spans.push_back(Span{token, std::move(text)});
    }

    void pad(int indent) {
        if (!cur.spans.empty() || indent <= 0) return;
        add(Token::Punct, std::wstring(static_cast<size_t>(indent), L' '));
    }
};

std::wstring TrimRight(std::wstring text) {
    while (!text.empty() && (text.back() == L' ' || text.back() == L'\t' || text.back() == L'\r'))
        text.pop_back();
    return text;
}

std::wstring Trim(std::wstring text) {
    size_t i = 0;
    while (i < text.size() && (text[i] == L' ' || text[i] == L'\t')) ++i;
    text.erase(0, i);
    return TrimRight(std::move(text));
}

bool IsNumber(const std::wstring& text) {
    if (text.empty()) return false;
    size_t i = 0;
    if (text[i] == L'+' || text[i] == L'-') ++i;
    if (i >= text.size() || !iswdigit(text[i])) return false;
    while (i < text.size() && iswdigit(text[i])) ++i;
    if (i < text.size() && text[i] == L'.') {
        ++i;
        if (i >= text.size() || !iswdigit(text[i])) return false;
        while (i < text.size() && iswdigit(text[i])) ++i;
    }
    if (i < text.size() && (text[i] == L'e' || text[i] == L'E')) {
        ++i;
        if (i < text.size() && (text[i] == L'+' || text[i] == L'-')) ++i;
        if (i >= text.size() || !iswdigit(text[i])) return false;
        while (i < text.size() && iswdigit(text[i])) ++i;
    }
    return i == text.size();
}

void AddScalar(Out& out, const std::wstring& text) {
    if (text.empty()) return;
    if (text.front() == L'"' || text.front() == L'\'') {
        out.add(Token::String, text);
        return;
    }
    if (text == L"true" || text == L"false" || text == L"yes" || text == L"no" ||
        text == L"True" || text == L"False" || text == L"YES" || text == L"NO" ||
        text == L"on" || text == L"off") {
        out.add(Token::Bool, text);
        return;
    }
    if (text == L"null" || text == L"Null" || text == L"NULL" || text == L"~") {
        out.add(Token::Null, text);
        return;
    }
    if (IsNumber(text)) {
        out.add(Token::Number, text);
        return;
    }
    out.add(Token::Text, text);
}

size_t FindUnquoted(const std::wstring& text, size_t from, wchar_t mark) {
    bool quoted = false;
    wchar_t quote = 0;
    for (size_t i = from; i < text.size(); ++i) {
        if (quoted) {
            if (text[i] == quote) quoted = false;
            continue;
        }
        if (text[i] == L'"' || text[i] == L'\'') {
            quoted = true;
            quote = text[i];
            continue;
        }
        if (text[i] == mark) return i;
    }
    return std::wstring::npos;
}

class JsonParser {
public:
    explicit JsonParser(const std::wstring& text) : in_(text) {}

    ViewDoc Run() {
        ViewDoc doc;
        doc.format = L"JSON";
        Skip();
        if (i_ >= in_.size()) {
            doc.error = L"пустой файл";
            return doc;
        }
        if (!ParseValue(0)) {
            doc.error = error_.empty() ? L"не разобран" : error_;
        } else {
            Skip();
            if (i_ < in_.size() && error_.empty()) error_ = L"лишний текст после значения";
            if (!out_.cur.spans.empty()) out_.brk();
            doc.lines = std::move(out_.lines);
            doc.error = error_;
        }
        if (doc.lines.empty()) doc.lines = HighlightLoose(in_);
        return doc;
    }

private:
    const std::wstring& in_;
    size_t i_ = 0;
    std::wstring error_;
    Out out_;

    void Fail(const wchar_t* text) {
        if (error_.empty()) error_ = text;
    }

    void Skip() {
        while (i_ < in_.size() && iswspace(in_[i_])) ++i_;
    }

    bool Eat(wchar_t ch) {
        Skip();
        if (i_ < in_.size() && in_[i_] == ch) {
            ++i_;
            return true;
        }
        return false;
    }

    bool ParseString(std::wstring& token) {
        Skip();
        if (i_ >= in_.size() || in_[i_] != L'"') return false;
        const size_t start = i_++;
        while (i_ < in_.size()) {
            if (in_[i_] == L'\\') {
                i_ += (i_ + 1 < in_.size()) ? 2 : 1;
                continue;
            }
            if (in_[i_] == L'"') {
                ++i_;
                token = in_.substr(start, i_ - start);
                return true;
            }
            ++i_;
        }
        Fail(L"незакрытая строка");
        return false;
    }

    bool ParseNumber() {
        const size_t start = i_;
        if (i_ < in_.size() && in_[i_] == L'-') ++i_;
        if (i_ >= in_.size() || !iswdigit(in_[i_])) {
            Fail(L"ожидалось число");
            return false;
        }
        if (in_[i_] == L'0') ++i_;
        else while (i_ < in_.size() && iswdigit(in_[i_])) ++i_;
        if (i_ < in_.size() && in_[i_] == L'.') {
            ++i_;
            if (i_ >= in_.size() || !iswdigit(in_[i_])) {
                Fail(L"ожидалась дробная часть");
                return false;
            }
            while (i_ < in_.size() && iswdigit(in_[i_])) ++i_;
        }
        if (i_ < in_.size() && (in_[i_] == L'e' || in_[i_] == L'E')) {
            ++i_;
            if (i_ < in_.size() && (in_[i_] == L'+' || in_[i_] == L'-')) ++i_;
            if (i_ >= in_.size() || !iswdigit(in_[i_])) {
                Fail(L"ожидался порядок числа");
                return false;
            }
            while (i_ < in_.size() && iswdigit(in_[i_])) ++i_;
        }
        out_.add(Token::Number, in_.substr(start, i_ - start));
        return true;
    }

    bool Word(const wchar_t* lit, Token token) {
        const size_t n = wcslen(lit);
        if (in_.compare(i_, n, lit) != 0) return false;
        if (i_ + n < in_.size() && iswalnum(in_[i_ + n])) return false;
        out_.add(token, lit);
        i_ += n;
        return true;
    }

    bool ParseContainer(wchar_t open, wchar_t close, bool object, int depth) {
        ++i_;
        Skip();
        if (i_ < in_.size() && in_[i_] == close) {
            ++i_;
            out_.add(Token::Punct, std::wstring{open, close});
            return true;
        }
        if (depth >= kMaxDepth) {
            Fail(L"слишком глубокая вложенность");
            return false;
        }
        out_.add(Token::Punct, std::wstring(1, open));
        out_.brk();
        bool needComma = false;
        while (true) {
            Skip();
            if (i_ < in_.size() && in_[i_] == close) break;
            if (i_ >= in_.size()) {
                Fail(object ? L"не закрыт объект" : L"не закрыт массив");
                return false;
            }
            if (needComma) {
                Fail(L"ожидалась запятая");
                return false;
            }
            out_.pad((depth + 1) * 2);
            if (object) {
                std::wstring key;
                if (!ParseString(key)) {
                    Fail(L"ожидался ключ");
                    return false;
                }
                out_.add(Token::Key, key);
                if (!Eat(L':')) {
                    Fail(L"ожидалось двоеточие");
                    return false;
                }
                out_.add(Token::Punct, L": ");
            }
            if (!ParseValue(depth + 1)) return false;
            Skip();
            if (Eat(L',')) {
                out_.add(Token::Punct, L",");
                out_.brk();
                continue;
            }
            needComma = true;
        }
        ++i_;
        out_.pad(depth * 2);
        out_.add(Token::Punct, std::wstring(1, close));
        return error_.empty();
    }

    bool ParseValue(int depth) {
        Skip();
        if (i_ >= in_.size()) {
            Fail(L"ожидалось значение");
            return false;
        }
        const wchar_t ch = in_[i_];
        if (ch == L'"') {
            std::wstring token;
            if (!ParseString(token)) return false;
            out_.add(Token::String, token);
            return true;
        }
        if (ch == L'{') return ParseContainer(L'{', L'}', true, depth);
        if (ch == L'[') return ParseContainer(L'[', L']', false, depth);
        if (ch == L't' && Word(L"true", Token::Bool)) return true;
        if (ch == L'f' && Word(L"false", Token::Bool)) return true;
        if (ch == L'n' && Word(L"null", Token::Null)) return true;
        if (ch == L'-' || iswdigit(ch)) return ParseNumber();
        Fail(L"неизвестное значение");
        return false;
    }
};

struct YLine {
    int indent = 0;
    std::wstring body;
    std::wstring comment;
    bool blank = false;
};

std::vector<YLine> SplitYaml(const std::wstring& text) {
    std::vector<YLine> lines;
    size_t start = 0;
    auto push = [&](std::wstring row) {
        row = TrimRight(std::move(row));
        YLine line;
        size_t i = 0;
        while (i < row.size() && (row[i] == L' ' || row[i] == L'\t')) {
            line.indent += row[i] == L'\t' ? 2 : 1;
            ++i;
        }
        const size_t hash = FindUnquoted(row, i, L'#');
        if (hash == std::wstring::npos) {
            line.body = Trim(row.substr(i));
        } else {
            line.body = Trim(row.substr(i, hash - i));
            line.comment = TrimRight(row.substr(hash));
        }
        line.blank = line.body.empty() && line.comment.empty();
        lines.push_back(std::move(line));
    };
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'\n') continue;
        std::wstring row = text.substr(start, i - start);
        if (!row.empty() && row.back() == L'\r') row.pop_back();
        push(std::move(row));
        start = i + 1;
    }
    if (start < text.size() || lines.empty()) {
        std::wstring row = text.substr(start);
        if (!row.empty() && row.back() == L'\r') row.pop_back();
        push(std::move(row));
    }
    return lines;
}

size_t YamlColon(const std::wstring& text) {
    bool quoted = false;
    wchar_t quote = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (quoted) {
            if (text[i] == quote) quoted = false;
            continue;
        }
        if (text[i] == L'"' || text[i] == L'\'') {
            quoted = true;
            quote = text[i];
            continue;
        }
        if (text[i] == L':' && (i + 1 == text.size() || text[i + 1] == L' ' || text[i + 1] == L'\t' || text[i + 1] == L'#'))
            return i;
    }
    return std::wstring::npos;
}

bool IsBlockScalar(const std::wstring& text) {
    return !text.empty() && (text[0] == L'|' || text[0] == L'>');
}

void EmitComment(Out& out, int depth, const std::wstring& comment) {
    if (comment.empty()) return;
    if (!out.cur.spans.empty()) out.add(Token::Punct, L"  ");
    else out.pad(depth * 2);
    out.add(Token::Comment, comment);
}

class YamlParser {
public:
    explicit YamlParser(std::vector<YLine> lines) : lines_(std::move(lines)) {}

    ViewDoc Run() {
        ViewDoc doc;
        doc.format = L"YAML";
        ParseBlock(-1, 0);
        if (!out_.cur.spans.empty()) out_.brk();
        doc.lines = std::move(out_.lines);
        doc.error = error_;
        return doc;
    }

private:
    std::vector<YLine> lines_;
    size_t i_ = 0;
    std::wstring error_;
    Out out_;

    size_t ParseLiteral(int parentIndent, int depth) {
        if (i_ >= lines_.size() || lines_[i_].blank || lines_[i_].indent <= parentIndent) return i_;
        const int base = lines_[i_].indent;
        while (i_ < lines_.size()) {
            if (lines_[i_].blank) {
                ++i_;
                continue;
            }
            if (lines_[i_].indent < base) break;
            out_.pad(depth * 2 + (lines_[i_].indent - base));
            if (!lines_[i_].body.empty()) out_.add(Token::String, lines_[i_].body);
            EmitComment(out_, depth, lines_[i_].comment);
            out_.brk();
            ++i_;
        }
        return i_;
    }

    size_t ParseBlock(int parentIndent, int depth) {
        if (depth > kMaxDepth) {
            if (error_.empty()) error_ = L"слишком глубокая вложенность";
            return lines_.size();
        }
        while (i_ < lines_.size() && lines_[i_].blank) ++i_;
        if (i_ >= lines_.size() || lines_[i_].indent <= parentIndent) return i_;
        const int level = lines_[i_].indent;
        while (i_ < lines_.size()) {
            if (lines_[i_].blank) {
                ++i_;
                continue;
            }
            if (lines_[i_].indent < level) break;
            if (lines_[i_].indent > level) {
                out_.pad(depth * 2);
                out_.add(Token::Text, lines_[i_].body);
                EmitComment(out_, depth, lines_[i_].comment);
                out_.brk();
                ++i_;
                continue;
            }
            if (lines_[i_].body.empty()) {
                out_.pad(depth * 2);
                EmitComment(out_, depth, lines_[i_].comment);
                out_.brk();
                ++i_;
                continue;
            }
            if (lines_[i_].body == L"---" || lines_[i_].body == L"...") {
                out_.add(Token::Punct, lines_[i_].body);
                EmitComment(out_, 0, lines_[i_].comment);
                out_.brk();
                ++i_;
                continue;
            }
            if (lines_[i_].body == L"-" || lines_[i_].body.rfind(L"- ", 0) == 0) {
                ParseSeq(level, depth);
                continue;
            }
            ParseMap(level, depth);
        }
        return i_;
    }

    void ParseSeq(int level, int depth) {
        std::wstring rest = lines_[i_].body.size() > 1 ? Trim(lines_[i_].body.substr(1)) : L"";
        const std::wstring comment = lines_[i_].comment;
        out_.pad(depth * 2);
        out_.add(Token::Punct, L"- ");
        const size_t colon = YamlColon(rest);
        if (rest.empty()) {
            EmitComment(out_, depth, comment);
            out_.brk();
            ++i_;
            ParseBlock(level, depth + 1);
            return;
        }
        if (colon != std::wstring::npos) {
            out_.add(Token::Key, Trim(rest.substr(0, colon)));
            out_.add(Token::Punct, L":");
            std::wstring value = Trim(rest.substr(colon + 1));
            if (!value.empty() && !IsBlockScalar(value)) {
                out_.add(Token::Punct, L" ");
                AddScalar(out_, value);
            } else if (IsBlockScalar(value)) {
                out_.add(Token::Punct, L" ");
                out_.add(Token::Punct, value);
            }
            EmitComment(out_, depth, comment);
            out_.brk();
            ++i_;
            if (value.empty()) ParseBlock(level, depth + 1);
            else if (IsBlockScalar(value)) ParseLiteral(level, depth + 1);
            return;
        }
        AddScalar(out_, rest);
        EmitComment(out_, depth, comment);
        out_.brk();
        ++i_;
        if (i_ < lines_.size() && !lines_[i_].blank && lines_[i_].indent > level)
            ParseBlock(level, depth + 1);
    }

    void ParseMap(int level, int depth) {
        const std::wstring body = lines_[i_].body;
        const std::wstring comment = lines_[i_].comment;
        const size_t colon = YamlColon(body);
        out_.pad(depth * 2);
        if (colon == std::wstring::npos) {
            AddScalar(out_, body);
            EmitComment(out_, depth, comment);
            out_.brk();
            ++i_;
            return;
        }
        out_.add(Token::Key, Trim(body.substr(0, colon)));
        out_.add(Token::Punct, L":");
        std::wstring value = Trim(body.substr(colon + 1));
        if (!value.empty() && !IsBlockScalar(value)) {
            out_.add(Token::Punct, L" ");
            AddScalar(out_, value);
        } else if (IsBlockScalar(value)) {
            out_.add(Token::Punct, L" ");
            out_.add(Token::Punct, value);
        }
        EmitComment(out_, depth, comment);
        out_.brk();
        ++i_;
        if (value.empty()) ParseBlock(level, depth + 1);
        else if (IsBlockScalar(value)) ParseLiteral(level, depth + 1);
    }
};

class XmlParser {
public:
    explicit XmlParser(const std::wstring& text) : in_(text) {}

    ViewDoc Run() {
        ViewDoc doc;
        doc.format = L"XML";
        Skip();
        while (i_ < in_.size() && error_.empty()) {
            Skip();
            if (i_ >= in_.size()) break;
            if (!ParseNode(0, false)) break;
        }
        if (!out_.cur.spans.empty()) out_.brk();
        doc.lines = std::move(out_.lines);
        doc.error = error_;
        if (doc.lines.empty()) doc.lines = HighlightLoose(in_);
        return doc;
    }

private:
    const std::wstring& in_;
    size_t i_ = 0;
    std::wstring error_;
    Out out_;

    void Fail(const wchar_t* text) {
        if (error_.empty()) error_ = text;
    }

    void Skip() {
        while (i_ < in_.size() && iswspace(in_[i_])) ++i_;
    }

    bool Starts(const wchar_t* lit) const {
        for (size_t k = 0; lit[k]; ++k) {
            if (i_ + k >= in_.size() || in_[i_ + k] != lit[k]) return false;
        }
        return true;
    }

    bool ParseNode(int depth, bool stopAtClose) {
        if (depth > kMaxDepth) {
            Fail(L"слишком глубокая вложенность");
            return false;
        }
        if (Starts(L"</")) return !stopAtClose ? (Fail(L"лишний закрывающий тег"), false) : false;
        if (Starts(L"<!--")) return ParseUntil(L"-->", Token::Comment, depth, L"не закрыт комментарий");
        if (Starts(L"<![CDATA[")) return ParseUntil(L"]]>", Token::String, depth, L"не закрыт CDATA");
        if (Starts(L"<?")) return ParseUntil(L"?>", Token::Punct, depth, L"не закрыта инструкция");
        if (Starts(L"<!") ) return ParseDoctype(depth);
        if (in_[i_] == L'<') return ParseElement(depth);
        return ParseText(depth);
    }

    bool ParseUntil(const wchar_t* end, Token token, int depth, const wchar_t* fail) {
        const size_t start = i_;
        const size_t n = wcslen(end);
        i_ += 2;
        while (i_ + n <= in_.size()) {
            if (in_.compare(i_, n, end) == 0) {
                i_ += n;
                out_.pad(depth * 2);
                out_.add(token, in_.substr(start, i_ - start));
                out_.brk();
                return true;
            }
            ++i_;
        }
        Fail(fail);
        return false;
    }

    bool ParseDoctype(int depth) {
        const size_t start = i_;
        int brackets = 0;
        ++i_;
        while (i_ < in_.size()) {
            if (in_[i_] == L'[') ++brackets;
            else if (in_[i_] == L']' && brackets) --brackets;
            else if (in_[i_] == L'>' && brackets == 0) {
                ++i_;
                out_.pad(depth * 2);
                out_.add(Token::Punct, in_.substr(start, i_ - start));
                out_.brk();
                return true;
            }
            ++i_;
        }
        Fail(L"не закрыт doctype");
        return false;
    }

    bool ParseText(int depth) {
        const size_t start = i_;
        while (i_ < in_.size() && in_[i_] != L'<') ++i_;
        std::wstring text = Trim(in_.substr(start, i_ - start));
        if (text.empty()) return true;
        out_.pad(depth * 2);
        out_.add(Token::Text, text);
        out_.brk();
        return true;
    }

    bool ParseElement(int depth) {
        ++i_;
        const size_t nameStart = i_;
        while (i_ < in_.size() && (iswalnum(in_[i_]) || in_[i_] == L':' || in_[i_] == L'_' || in_[i_] == L'-' || in_[i_] == L'.'))
            ++i_;
        if (i_ == nameStart) {
            Fail(L"ожидалось имя тега");
            return false;
        }
        const std::wstring name = in_.substr(nameStart, i_ - nameStart);
        struct Attr { std::wstring name, value; bool single; };
        std::vector<Attr> attrs;
        while (true) {
            Skip();
            if (i_ >= in_.size()) {
                Fail(L"не закрыт тег");
                return false;
            }
            if (in_[i_] == L'>' || in_[i_] == L'/') break;
            const size_t aStart = i_;
            while (i_ < in_.size() && (iswalnum(in_[i_]) || in_[i_] == L':' || in_[i_] == L'_' || in_[i_] == L'-' || in_[i_] == L'.'))
                ++i_;
            if (i_ == aStart) {
                Fail(L"ожидался атрибут");
                return false;
            }
            Attr attr;
            attr.name = in_.substr(aStart, i_ - aStart);
            Skip();
            if (i_ >= in_.size() || in_[i_] != L'=') {
                Fail(L"ожидалось =");
                return false;
            }
            ++i_;
            Skip();
            if (i_ >= in_.size() || (in_[i_] != L'"' && in_[i_] != L'\'')) {
                Fail(L"ожидалось значение атрибута");
                return false;
            }
            const wchar_t quote = in_[i_++];
            const size_t vStart = i_;
            while (i_ < in_.size() && in_[i_] != quote) ++i_;
            if (i_ >= in_.size()) {
                Fail(L"не закрыт атрибут");
                return false;
            }
            attr.value = in_.substr(vStart, i_ - vStart);
            attr.single = quote == L'\'';
            ++i_;
            attrs.push_back(std::move(attr));
        }
        bool self = false;
        if (in_[i_] == L'/') {
            self = true;
            ++i_;
        }
        if (i_ >= in_.size() || in_[i_] != L'>') {
            Fail(L"ожидался >");
            return false;
        }
        ++i_;

        out_.pad(depth * 2);
        out_.add(Token::Punct, L"<");
        out_.add(Token::Tag, name);
        for (const auto& attr : attrs) {
            out_.add(Token::Punct, L" ");
            out_.add(Token::Attr, attr.name);
            out_.add(Token::Punct, L"=");
            out_.add(Token::String, std::wstring(1, attr.single ? L'\'' : L'"') + attr.value + (attr.single ? L"'" : L"\""));
        }
        if (self) {
            out_.add(Token::Punct, L"/>");
            out_.brk();
            return true;
        }
        out_.add(Token::Punct, L">");

        const size_t after = i_;
        Skip();
        if (Starts(L"</")) {
            std::wstring inner = Trim(in_.substr(after, i_ - after));
            if (!CloseTag(name)) return false;
            if (inner.empty() || (inner.find(L'<') == std::wstring::npos && inner.size() <= 64)) {
                if (!inner.empty()) out_.add(Token::Text, inner);
                out_.add(Token::Punct, L"</");
                out_.add(Token::Tag, name);
                out_.add(Token::Punct, L">");
                out_.brk();
                return true;
            }
            out_.brk();
            out_.pad(depth * 2);
            out_.add(Token::Text, inner);
            out_.brk();
            out_.pad(depth * 2);
            out_.add(Token::Punct, L"</");
            out_.add(Token::Tag, name);
            out_.add(Token::Punct, L">");
            out_.brk();
            return true;
        }

        out_.brk();
        while (i_ < in_.size() && !Starts(L"</")) {
            Skip();
            if (i_ >= in_.size()) {
                Fail(L"не закрыт элемент");
                return false;
            }
            if (Starts(L"</")) break;
            if (!ParseNode(depth + 1, true)) return false;
        }
        if (!CloseTag(name)) return false;
        out_.pad(depth * 2);
        out_.add(Token::Punct, L"</");
        out_.add(Token::Tag, name);
        out_.add(Token::Punct, L">");
        out_.brk();
        return true;
    }

    bool CloseTag(const std::wstring& name) {
        if (!Starts(L"</")) {
            Fail(L"ожидался закрывающий тег");
            return false;
        }
        i_ += 2;
        Skip();
        if (in_.compare(i_, name.size(), name) != 0) {
            Fail(L"закрывающий тег не совпал");
            return false;
        }
        i_ += name.size();
        Skip();
        if (i_ >= in_.size() || in_[i_] != L'>') {
            Fail(L"ожидался >");
            return false;
        }
        ++i_;
        return true;
    }
};

std::vector<ViewLine> HighlightLoose(const std::wstring& text) {
    std::vector<ViewLine> lines;
    ViewLine cur;
    auto brk = [&] {
        lines.push_back(std::move(cur));
        cur.spans.clear();
    };
    for (size_t i = 0; i < text.size();) {
        if (text[i] == L'\n') {
            brk();
            ++i;
            continue;
        }
        if (text[i] == L'\r') {
            ++i;
            continue;
        }
        if (text[i] == L'"' || text[i] == L'\'') {
            const wchar_t quote = text[i];
            const size_t start = i++;
            while (i < text.size() && text[i] != quote && text[i] != L'\n') {
                if (text[i] == L'\\' && i + 1 < text.size()) i += 2;
                else ++i;
            }
            if (i < text.size() && text[i] == quote) ++i;
            cur.spans.push_back({Token::String, text.substr(start, i - start)});
            continue;
        }
        if (text.compare(i, 2, L"//") == 0 || text[i] == L'#') {
            const size_t start = i;
            while (i < text.size() && text[i] != L'\n') ++i;
            cur.spans.push_back({Token::Comment, text.substr(start, i - start)});
            continue;
        }
        if (iswdigit(text[i]) || (text[i] == L'-' && i + 1 < text.size() && iswdigit(text[i + 1]))) {
            const size_t start = i++;
            while (i < text.size() && (iswdigit(text[i]) || text[i] == L'.')) ++i;
            cur.spans.push_back({Token::Number, text.substr(start, i - start)});
            continue;
        }
        const size_t start = i++;
        while (i < text.size() && text[i] != L'\n' && text[i] != L'"' && text[i] != L'\'' && text[i] != L'#') ++i;
        cur.spans.push_back({Token::Text, text.substr(start, i - start)});
    }
    if (!cur.spans.empty() || lines.empty()) brk();
    return lines;
}

void PrependError(ViewDoc& doc) {
    if (doc.error.empty()) return;
    ViewLine line;
    line.spans.push_back({Token::Error, L"ошибка: " + doc.error});
    doc.lines.insert(doc.lines.begin(), std::move(line));
}

}  // namespace

std::wstring JoinLine(const ViewLine& line) {
    std::wstring text;
    for (const auto& span : line.spans) text += span.text;
    return text;
}

std::vector<ViewLine> SplitRaw(const std::wstring& text) {
    return HighlightLoose(text);
}

ViewDoc FormatDocument(const std::wstring& text, const std::wstring& extension) {
    std::wstring body = text;
    if (!body.empty() && body[0] == 0xFEFF) body.erase(body.begin());
    ViewDoc doc;
    if (extension == L".json") doc = JsonParser(body).Run();
    else if (extension == L".yaml" || extension == L".yml") doc = YamlParser(SplitYaml(body)).Run();
    else if (extension == L".xml") doc = XmlParser(body).Run();
    else {
        doc.format = L"?";
        doc.error = L"неизвестный формат";
        doc.lines = HighlightLoose(body);
    }
    PrependError(doc);
    if (!doc.error.empty() && doc.lines.size() == 1) {
        auto loose = HighlightLoose(body);
        doc.lines.insert(doc.lines.end(), loose.begin(), loose.end());
    }
    return doc;
}
