#include "markdown_parser.hpp"
#include <algorithm>
#include <regex>
#include <set>
#include <map>

static std::wstring TrimLeft(const std::wstring& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) ++i;
    return s.substr(i);
}

static std::wstring TrimRight(const std::wstring& s) {
    size_t i = s.size();
    while (i > 0 && (s[i - 1] == L' ' || s[i - 1] == L'\t')) --i;
    return s.substr(0, i);
}

static std::wstring Trim(const std::wstring& s) {
    return TrimLeft(TrimRight(s));
}

static std::wstring ToLower(const std::wstring& s) {
    std::wstring result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::towlower);
    return result;
}

static bool IsIdentChar(wchar_t ch) {
    return iswalnum(ch) || ch == L'_';
}

static bool IsDigitChar(wchar_t ch) {
    return iswdigit(ch);
}

struct LanguageKeywords {
    std::set<std::wstring> keywords;
    std::set<std::wstring> types;
    std::wstring singleLineComment;
    std::wstring multiLineCommentStart;
    std::wstring multiLineCommentEnd;
    bool hasPreprocessor;
};

static std::map<std::wstring, LanguageKeywords> g_languages;
static bool g_languagesInitialized = false;

static void InitLanguages() {
    if (g_languagesInitialized) return;
    g_languagesInitialized = true;

    // C/C++
    LanguageKeywords cpp;
    cpp.keywords = {L"if", L"else", L"for", L"while", L"do", L"switch", L"case", L"break",
                    L"continue", L"return", L"goto", L"default", L"typedef", L"struct", L"enum",
                    L"union", L"class", L"namespace", L"template", L"typename", L"public", L"private",
                    L"protected", L"virtual", L"override", L"final", L"const", L"static", L"extern",
                    L"inline", L"volatile", L"register", L"auto", L"new", L"delete", L"try", L"catch",
                    L"throw", L"sizeof", L"alignof", L"decltype", L"noexcept", L"nullptr", L"true",
                    L"false", L"this", L"using", L"friend", L"operator", L"explicit", L"mutable",
                    L"constexpr", L"static_cast", L"dynamic_cast", L"const_cast", L"reinterpret_cast"};
    cpp.types = {L"int", L"char", L"float", L"double", L"void", L"bool", L"long", L"short",
                 L"unsigned", L"signed", L"size_t", L"ptrdiff_t", L"int8_t", L"int16_t", L"int32_t",
                 L"int64_t", L"uint8_t", L"uint16_t", L"uint32_t", L"uint64_t", L"string", L"vector",
                 L"map", L"set", L"list", L"array", L"pair", L"tuple", L"shared_ptr", L"unique_ptr",
                 L"weak_ptr", L"optional", L"variant", L"any"};
    cpp.singleLineComment = L"//";
    cpp.multiLineCommentStart = L"/*";
    cpp.multiLineCommentEnd = L"*/";
    cpp.hasPreprocessor = true;
    g_languages[L"c"] = cpp;
    g_languages[L"cpp"] = cpp;
    g_languages[L"c++"] = cpp;
    g_languages[L"cxx"] = cpp;
    g_languages[L"cc"] = cpp;
    g_languages[L"h"] = cpp;
    g_languages[L"hpp"] = cpp;

    // Python
    LanguageKeywords python;
    python.keywords = {L"if", L"elif", L"else", L"for", L"while", L"break", L"continue", L"return",
                       L"yield", L"pass", L"raise", L"try", L"except", L"finally", L"with", L"as",
                       L"import", L"from", L"class", L"def", L"lambda", L"and", L"or", L"not", L"in",
                       L"is", L"del", L"global", L"nonlocal", L"assert", L"async", L"await", L"True",
                       L"False", L"None"};
    python.types = {L"int", L"float", L"str", L"bool", L"list", L"dict", L"tuple", L"set", L"frozenset",
                    L"bytes", L"bytearray", L"memoryview", L"complex", L"range", L"type", L"object"};
    python.singleLineComment = L"#";
    python.multiLineCommentStart = L"\"\"\"";
    python.multiLineCommentEnd = L"\"\"\"";
    python.hasPreprocessor = false;
    g_languages[L"python"] = python;
    g_languages[L"py"] = python;

    // JavaScript/TypeScript
    LanguageKeywords js;
    js.keywords = {L"if", L"else", L"for", L"while", L"do", L"switch", L"case", L"break", L"continue",
                   L"return", L"function", L"class", L"extends", L"new", L"this", L"super", L"import",
                   L"export", L"default", L"from", L"const", L"let", L"var", L"try", L"catch", L"finally",
                   L"throw", L"typeof", L"instanceof", L"in", L"of", L"async", L"await", L"yield",
                   L"true", L"false", L"null", L"undefined", L"void", L"delete", L"debugger", L"with"};
    js.types = {L"Number", L"String", L"Boolean", L"Object", L"Array", L"Function", L"Symbol", L"Map",
                L"Set", L"WeakMap", L"WeakSet", L"Promise", L"RegExp", L"Date", L"Error", L"NaN",
                L"Infinity", L"parseInt", L"parseFloat", L"isNaN", L"isFinite"};
    js.singleLineComment = L"//";
    js.multiLineCommentStart = L"/*";
    js.multiLineCommentEnd = L"*/";
    js.hasPreprocessor = false;
    g_languages[L"javascript"] = js;
    g_languages[L"js"] = js;
    g_languages[L"typescript"] = js;
    g_languages[L"ts"] = js;

    // Rust
    LanguageKeywords rust;
    rust.keywords = {L"as", L"break", L"const", L"continue", L"crate", L"else", L"enum", L"extern",
                     L"false", L"fn", L"for", L"if", L"impl", L"in", L"let", L"loop", L"match", L"mod",
                     L"move", L"mut", L"pub", L"ref", L"return", L"self", L"Self", L"static", L"struct",
                     L"super", L"trait", L"true", L"type", L"unsafe", L"use", L"where", L"while", L"async",
                     L"await", L"dyn", L"abstract", L"become", L"box", L"do", L"final", L"macro",
                     L"override", L"priv", L"typeof", L"unsized", L"virtual", L"yield"};
    rust.types = {L"i8", L"i16", L"i32", L"i64", L"i128", L"isize", L"u8", L"u16", L"u32", L"u64",
                  L"u128", L"usize", L"f32", L"f64", L"bool", L"char", L"str", L"String", L"Vec",
                  L"Option", L"Result", L"Box", L"Rc", L"Arc", L"Cell", L"RefCell", L"HashMap", L"BTreeMap"};
    rust.singleLineComment = L"//";
    rust.multiLineCommentStart = L"/*";
    rust.multiLineCommentEnd = L"*/";
    rust.hasPreprocessor = false;
    g_languages[L"rust"] = rust;
    g_languages[L"rs"] = rust;

    // Go
    LanguageKeywords go;
    go.keywords = {L"break", L"default", L"func", L"interface", L"select", L"case", L"defer", L"go",
                   L"map", L"struct", L"chan", L"else", L"goto", L"package", L"switch", L"const", L"fallthrough",
                   L"if", L"range", L"type", L"continue", L"for", L"import", L"return", L"var", L"true",
                   L"false", L"nil", L"iota"};
    go.types = {L"int", L"int8", L"int16", L"int32", L"int64", L"uint", L"uint8", L"uint16", L"uint32",
                L"uint64", L"uintptr", L"float32", L"float64", L"complex64", L"complex128", L"string",
                L"bool", L"byte", L"rune", L"error", L"any", L"comparable"};
    go.singleLineComment = L"//";
    go.multiLineCommentStart = L"/*";
    go.multiLineCommentEnd = L"*/";
    go.hasPreprocessor = false;
    g_languages[L"go"] = go;
    g_languages[L"golang"] = go;

    // Java
    LanguageKeywords java;
    java.keywords = {L"abstract", L"assert", L"break", L"case", L"catch", L"class", L"const", L"continue",
                     L"default", L"do", L"else", L"enum", L"extends", L"final", L"finally", L"for", L"goto",
                     L"if", L"implements", L"import", L"instanceof", L"interface", L"native", L"new", L"package",
                     L"private", L"protected", L"public", L"return", L"static", L"strictfp", L"super", L"switch",
                     L"synchronized", L"this", L"throw", L"throws", L"transient", L"try", L"volatile", L"while",
                     L"true", L"false", L"null"};
    java.types = {L"int", L"byte", L"short", L"long", L"float", L"double", L"char", L"boolean", L"void",
                  L"String", L"Integer", L"Long", L"Double", L"Float", L"Boolean", L"Character", L"Byte",
                  L"Short", L"Object", L"Class", L"System", L"Thread", L"Runnable", L"Exception", L"Error"};
    java.singleLineComment = L"//";
    java.multiLineCommentStart = L"/*";
    java.multiLineCommentEnd = L"*/";
    java.hasPreprocessor = false;
    g_languages[L"java"] = java;

    // C#
    LanguageKeywords csharp;
    csharp.keywords = {L"abstract", L"as", L"base", L"bool", L"break", L"byte", L"case", L"catch", L"char",
                       L"checked", L"class", L"const", L"continue", L"decimal", L"default", L"delegate", L"do",
                       L"double", L"else", L"enum", L"event", L"explicit", L"extern", L"false", L"finally",
                       L"fixed", L"float", L"for", L"foreach", L"goto", L"if", L"implicit", L"in", L"int",
                       L"interface", L"internal", L"is", L"lock", L"long", L"namespace", L"new", L"null",
                       L"object", L"operator", L"out", L"override", L"params", L"private", L"protected",
                       L"public", L"readonly", L"ref", L"return", L"sbyte", L"sealed", L"short", L"sizeof",
                       L"stackalloc", L"static", L"string", L"struct", L"switch", L"this", L"throw", L"true",
                       L"try", L"typeof", L"uint", L"ulong", L"unchecked", L"unsafe", L"ushort", L"using",
                       L"virtual", L"void", L"volatile", L"while"};
    csharp.types = {L"int", L"long", L"short", L"byte", L"sbyte", L"uint", L"ulong", L"ushort", L"float",
                    L"double", L"decimal", L"char", L"bool", L"string", L"object", L"dynamic", L"var"};
    csharp.singleLineComment = L"//";
    csharp.multiLineCommentStart = L"/*";
    csharp.multiLineCommentEnd = L"*/";
    csharp.hasPreprocessor = true;
    g_languages[L"csharp"] = csharp;
    g_languages[L"cs"] = csharp;
    g_languages[L"c#"] = csharp;

    // SQL
    LanguageKeywords sql;
    sql.keywords = {L"SELECT", L"FROM", L"WHERE", L"INSERT", L"UPDATE", L"DELETE", L"CREATE", L"ALTER",
                    L"DROP", L"TABLE", L"INDEX", L"VIEW", L"PROCEDURE", L"FUNCTION", L"TRIGGER", L"DATABASE",
                    L"INTO", L"VALUES", L"SET", L"JOIN", L"LEFT", L"RIGHT", L"INNER", L"OUTER", L"ON",
                    L"AND", L"OR", L"NOT", L"IN", L"EXISTS", L"BETWEEN", L"LIKE", L"IS", L"NULL", L"AS",
                    L"ORDER", L"BY", L"GROUP", L"HAVING", L"LIMIT", L"OFFSET", L"UNION", L"ALL", L"DISTINCT",
                    L"CASE", L"WHEN", L"THEN", L"ELSE", L"END", L"BEGIN", L"COMMIT", L"ROLLBACK", L"TRANSACTION",
                    L"GRANT", L"REVOKE", L"PRIMARY", L"KEY", L"FOREIGN", L"REFERENCES", L"CONSTRAINT",
                    L"UNIQUE", L"CHECK", L"DEFAULT", L"AUTO_INCREMENT", L"IDENTITY"};
    sql.types = {L"INT", L"INTEGER", L"BIGINT", L"SMALLINT", L"TINYINT", L"DECIMAL", L"NUMERIC", L"FLOAT",
                 L"REAL", L"DOUBLE", L"CHAR", L"VARCHAR", L"TEXT", L"NCHAR", L"NVARCHAR", L"NTEXT", L"DATE",
                 L"TIME", L"DATETIME", L"TIMESTAMP", L"BOOLEAN", L"BLOB", L"CLOB"};
    sql.singleLineComment = L"--";
    sql.multiLineCommentStart = L"/*";
    sql.multiLineCommentEnd = L"*/";
    sql.hasPreprocessor = false;
    g_languages[L"sql"] = sql;

    // Shell/Bash
    LanguageKeywords shell;
    shell.keywords = {L"if", L"then", L"else", L"elif", L"fi", L"for", L"while", L"do", L"done", L"case",
                      L"esac", L"in", L"function", L"select", L"until", L"return", L"exit", L"break",
                      L"continue", L"shift", L"export", L"readonly", L"declare", L"local", L"unset",
                      L"eval", L"exec", L"source", L"true", L"false"};
    shell.types = {};
    shell.singleLineComment = L"#";
    shell.multiLineCommentStart = L"";
    shell.multiLineCommentEnd = L"";
    shell.hasPreprocessor = false;
    g_languages[L"bash"] = shell;
    g_languages[L"sh"] = shell;
    g_languages[L"shell"] = shell;
    g_languages[L"zsh"] = shell;

    // JSON
    LanguageKeywords json;
    json.keywords = {L"true", L"false", L"null"};
    json.types = {};
    json.singleLineComment = L"";
    json.multiLineCommentStart = L"";
    json.multiLineCommentEnd = L"";
    json.hasPreprocessor = false;
    g_languages[L"json"] = json;

    // YAML
    LanguageKeywords yaml;
    yaml.keywords = {L"true", L"false", L"null", L"yes", L"no", L"on", L"off"};
    yaml.types = {};
    yaml.singleLineComment = L"#";
    yaml.multiLineCommentStart = L"";
    yaml.multiLineCommentEnd = L"";
    yaml.hasPreprocessor = false;
    g_languages[L"yaml"] = yaml;
    g_languages[L"yml"] = yaml;

    // Ruby
    LanguageKeywords ruby;
    ruby.keywords = {L"BEGIN", L"END", L"alias", L"and", L"begin", L"break", L"case", L"class", L"def",
                     L"defined?", L"do", L"else", L"elsif", L"end", L"ensure", L"false", L"for", L"if",
                     L"in", L"module", L"next", L"nil", L"not", L"or", L"redo", L"rescue", L"retry",
                     L"return", L"self", L"super", L"then", L"true", L"undef", L"unless", L"until", L"when",
                     L"while", L"yield"};
    ruby.types = {L"Array", L"Hash", L"String", L"Integer", L"Float", L"Symbol", L"Range", L"Regexp",
                  L"Proc", L"Lambda", L"IO", L"File", L"Dir", L"Thread", L"Mutex", L"Queue"};
    ruby.singleLineComment = L"#";
    ruby.multiLineCommentStart = L"=begin";
    ruby.multiLineCommentEnd = L"=end";
    ruby.hasPreprocessor = false;
    g_languages[L"ruby"] = ruby;
    g_languages[L"rb"] = ruby;

    // PHP
    LanguageKeywords php;
    php.keywords = {L"abstract", L"and", L"array", L"as", L"break", L"callable", L"case", L"catch", L"class",
                    L"clone", L"const", L"continue", L"declare", L"default", L"die", L"do", L"echo", L"else",
                    L"elseif", L"empty", L"enddeclare", L"endfor", L"endforeach", L"endif", L"endswitch",
                    L"endwhile", L"eval", L"exit", L"extends", L"final", L"for", L"foreach", L"function",
                    L"global", L"goto", L"if", L"implements", L"include", L"include_once", L"instanceof",
                    L"interface", L"isset", L"list", L"namespace", L"new", L"or", L"print", L"private",
                    L"protected", L"public", L"require", L"require_once", L"return", L"static", L"switch",
                    L"throw", L"trait", L"try", L"unset", L"use", L"var", L"while", L"xor", L"yield",
                    L"true", L"false", L"null"};
    php.types = {L"int", L"float", L"string", L"bool", L"array", L"object", L"callable", L"iterable", L"void"};
    php.singleLineComment = L"//";
    php.multiLineCommentStart = L"/*";
    php.multiLineCommentEnd = L"*/";
    php.hasPreprocessor = false;
    g_languages[L"php"] = php;
}

int MarkdownParser::CountLeading(const std::wstring& line, wchar_t ch) {
    int count = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == ch) ++count;
        else break;
    }
    return count;
}

bool MarkdownParser::IsHorizontalRule(const std::wstring& trimmed) {
    if (trimmed.size() < 3) return false;

    auto countChar = [&](wchar_t ch) -> int {
        int c = 0;
        for (wchar_t w : trimmed) {
            if (w == ch) ++c;
            else if (w != L' ') return 0;
        }
        return c;
    };

    return countChar(L'-') >= 3 || countChar(L'*') >= 3 || countChar(L'_') >= 3;
}

bool MarkdownParser::IsTableSeparator(const std::wstring& trimmed) {
    if (trimmed.empty() || trimmed.find(L'|') == std::wstring::npos)
        return false;

    for (wchar_t ch : trimmed) {
        if (ch != L'|' && ch != L'-' && ch != L':' && ch != L' ')
            return false;
    }
    return trimmed.find(L'-') != std::wstring::npos;
}

LineStyle MarkdownParser::DetectLineStyle(const std::wstring& line) {
    fenceKind_ = 0;
    std::wstring trimmed = Trim(line);

    if (trimmed.empty())
        return inCodeBlock_ ? LineStyle::CodeBlock : LineStyle::Empty;

    if (inCodeBlock_) {
        if (trimmed.size() >= 3 && trimmed.substr(0, 3) == L"```") {
            fenceKind_ = 2;
            inCodeBlock_ = false;
            return LineStyle::CodeBlock;
        }
        return LineStyle::CodeBlock;
    }

    if (trimmed.size() >= 3 && trimmed.substr(0, 3) == L"```") {
        fenceKind_ = 1;
        inCodeBlock_ = true;
        std::wstring lang = Trim(trimmed.substr(3));
        currentCodeLanguage_ = ToLower(lang);
        return LineStyle::CodeBlock;
    }

    if (IsHorizontalRule(trimmed))
        return LineStyle::HorizontalRule;

    if (trimmed[0] == L'#') {
        int hashes = CountLeading(trimmed, L'#');
        if (hashes >= 1 && hashes <= 6 &&
            (hashes >= (int)trimmed.size() || trimmed[hashes] == L' ')) {
            switch (hashes) {
                case 1: return LineStyle::Header1;
                case 2: return LineStyle::Header2;
                case 3: return LineStyle::Header3;
                case 4: return LineStyle::Header4;
                case 5: return LineStyle::Header5;
                case 6: return LineStyle::Header6;
            }
        }
    }

    if (trimmed[0] == L'>')
        return LineStyle::Blockquote;

    if (trimmed.size() >= 2) {
        if ((trimmed[0] == L'-' || trimmed[0] == L'*' || trimmed[0] == L'+') &&
            trimmed[1] == L' ')
            return LineStyle::UnorderedList;
    }

    if (trimmed.size() >= 3 && trimmed[0] >= L'1' && trimmed[0] <= L'9') {
        size_t i = 0;
        while (i < trimmed.size() && trimmed[i] >= L'0' && trimmed[i] <= L'9') ++i;
        if (i < trimmed.size() && trimmed[i] == L'.' && i + 1 < trimmed.size() && trimmed[i + 1] == L' ')
            return LineStyle::OrderedList;
    }

    if (trimmed.find(L'|') != std::wstring::npos && trimmed[0] == L'|')
        return LineStyle::Table;

    if (trimmed.size() >= 2 && trimmed[0] == L'!' && trimmed[1] == L'[')
        return LineStyle::Image;

    return LineStyle::Normal;
}

std::wstring MarkdownParser::StripMarkdownSyntax(const std::wstring& line, LineStyle style) {
    std::wstring trimmed = Trim(line);

    switch (style) {
        case LineStyle::Header1:
        case LineStyle::Header2:
        case LineStyle::Header3:
        case LineStyle::Header4:
        case LineStyle::Header5:
        case LineStyle::Header6: {
            int hashes = CountLeading(trimmed, L'#');
            std::wstring content = Trim(trimmed.substr(hashes));
            if (!content.empty() && content.back() == L'#') {
                size_t end = content.size() - 1;
                while (end > 0 && content[end - 1] == L'#') --end;
                content = Trim(content.substr(0, end));
            }
            return content;
        }

        case LineStyle::Blockquote: {
            std::wstring content = trimmed.substr(1);
            if (!content.empty() && content[0] == L' ')
                content = content.substr(1);
            return content;
        }

        case LineStyle::UnorderedList: {
            return Trim(trimmed.substr(2));
        }

        case LineStyle::OrderedList: {
            size_t dot = trimmed.find(L'.');
            if (dot != std::wstring::npos)
                return Trim(trimmed.substr(dot + 2));
            return trimmed;
        }

        case LineStyle::Image: {
            size_t altEnd = trimmed.find(L']');
            if (altEnd != std::wstring::npos) {
                std::wstring alt = trimmed.substr(2, altEnd - 2);
                return L"[Image: " + alt + L"]";
            }
            return trimmed;
        }

        case LineStyle::CodeBlock:
            return TrimRight(line);

        case LineStyle::HorizontalRule:
        case LineStyle::Empty:
        case LineStyle::Table:
        case LineStyle::Normal:
        default:
            return trimmed;
    }
}

std::vector<StyledSegment> MarkdownParser::ParseInlineFormatting(const std::wstring& text, LineStyle lineStyle) {
    std::vector<StyledSegment> segments;

    if (lineStyle == LineStyle::CodeBlock) {
        segments.push_back(StyledSegment(text, SegmentStyle::CodeBlock));
        return segments;
    }

    if (lineStyle == LineStyle::HorizontalRule) {
        segments.push_back(StyledSegment(text, SegmentStyle::HorizontalRule));
        return segments;
    }

    SegmentStyle headerStyle = (lineStyle >= LineStyle::Header1 && lineStyle <= LineStyle::Header6)
        ? SegmentStyle::Header : SegmentStyle::Normal;

    size_t i = 0;
    std::wstring currentText;

    while (i < text.size()) {
        if (i + 1 < text.size() && text[i] == L'~' && text[i + 1] == L'~') {
            if (!currentText.empty()) {
                segments.push_back(StyledSegment(currentText, headerStyle));
                currentText.clear();
            }
            size_t start = i + 2;
            size_t end = text.find(L"~~", start);
            if (end != std::wstring::npos) {
                segments.push_back(StyledSegment(text.substr(start, end - start), SegmentStyle::Strike));
                i = end + 2;
            } else {
                currentText += text[i];
                ++i;
            }
        }
        else if (i + 1 < text.size() &&
            ((text[i] == L'*' && text[i + 1] == L'*') ||
             (text[i] == L'_' && text[i + 1] == L'_'))) {
            if (!currentText.empty()) {
                segments.push_back(StyledSegment(currentText, headerStyle));
                currentText.clear();
            }

            wchar_t marker = text[i];
            size_t start = i + 2;
            size_t end = text.find(std::wstring(2, marker), start);

            if (end != std::wstring::npos) {
                std::wstring boldText = text.substr(start, end - start);
                segments.push_back(StyledSegment(boldText, SegmentStyle::Bold));
                i = end + 2;
            } else {
                currentText += text[i];
                ++i;
            }
        }
        else if (text[i] == L'*' || text[i] == L'_') {
            if (!currentText.empty()) {
                segments.push_back(StyledSegment(currentText, headerStyle));
                currentText.clear();
            }

            wchar_t marker = text[i];
            size_t start = i + 1;
            size_t end = text.find(marker, start);

            if (end != std::wstring::npos && end > start) {
                std::wstring italicText = text.substr(start, end - start);
                segments.push_back(StyledSegment(italicText, SegmentStyle::Italic));
                i = end + 1;
            } else {
                currentText += text[i];
                ++i;
            }
        }
        else if (text[i] == L'`') {
            if (!currentText.empty()) {
                segments.push_back(StyledSegment(currentText, headerStyle));
                currentText.clear();
            }

            size_t start = i + 1;
            size_t end = text.find(L'`', start);

            if (end != std::wstring::npos) {
                std::wstring codeText = text.substr(start, end - start);
                segments.push_back(StyledSegment(codeText, SegmentStyle::Code));
                i = end + 1;
            } else {
                currentText += text[i];
                ++i;
            }
        }
        else if (text[i] == L'[' || (text[i] == L'!' && i + 1 < text.size() && text[i + 1] == L'[')) {
            if (!currentText.empty()) {
                segments.push_back(StyledSegment(currentText, headerStyle));
                currentText.clear();
            }

            bool isImage = (text[i] == L'!');
            size_t start = isImage ? i + 2 : i + 1;
            size_t bracketEnd = text.find(L']', start);

            if (bracketEnd != std::wstring::npos && bracketEnd + 1 < text.size() && text[bracketEnd + 1] == L'(') {
                size_t parenEnd = text.find(L')', bracketEnd + 2);
                if (parenEnd != std::wstring::npos) {
                    std::wstring linkText = text.substr(start, bracketEnd - start);
                    if (isImage) {
                        segments.push_back(StyledSegment(L"[Image: " + linkText + L"]", SegmentStyle::Image));
                    } else {
                        segments.push_back(StyledSegment(linkText, SegmentStyle::Link));
                    }
                    i = parenEnd + 1;
                } else {
                    currentText += text[i];
                    ++i;
                }
            } else {
                currentText += text[i];
                ++i;
            }
        }
        else {
            currentText += text[i];
            ++i;
        }
    }

    if (!currentText.empty()) {
        segments.push_back(StyledSegment(currentText, headerStyle));
    }

    if (segments.empty()) {
        segments.push_back(StyledSegment(text, headerStyle));
    }

    return segments;
}

std::vector<StyledSegment> MarkdownParser::HighlightCode(const std::wstring& code, const std::wstring& language) {
    InitLanguages();

    std::vector<StyledSegment> segments;

    auto it = g_languages.find(language);
    if (it == g_languages.end()) {
        segments.push_back(StyledSegment(code, SegmentStyle::CodeBlock));
        return segments;
    }

    const auto& lang = it->second;
    size_t i = 0;
    std::wstring currentToken;

    while (i < code.size()) {
        // Preprocessor directives
        if (lang.hasPreprocessor && code[i] == L'#' && (i == 0 || code[i - 1] == L'\n' || Trim(code.substr(0, i)).empty())) {
            if (!currentToken.empty()) {
                segments.push_back(StyledSegment(currentToken, SegmentStyle::CodeBlock));
                currentToken.clear();
            }
            const size_t start = i++;
            while (i < code.size() && (code[i] == L' ' || code[i] == L'\t')) ++i;
            while (i < code.size() && IsIdentChar(code[i])) ++i;
            segments.push_back(StyledSegment(code.substr(start, i - start), SegmentStyle::Preprocessor));
            size_t j = i;
            while (j < code.size() && (code[j] == L' ' || code[j] == L'\t')) ++j;
            if (j < code.size() && code[j] == L'<') {
                const size_t nl = code.find(L'\n', j);
                size_t end = code.find(L'>', j);
                if (end != std::wstring::npos && (nl == std::wstring::npos || end < nl)) {
                    ++end;
                    if (j > i)
                        segments.push_back(StyledSegment(code.substr(i, j - i), SegmentStyle::CodeBlock));
                    segments.push_back(StyledSegment(code.substr(j, end - j), SegmentStyle::StringType));
                    i = end;
                }
            }
            continue;
        }

        // Single-line comment
        if (!lang.singleLineComment.empty() &&
            i + lang.singleLineComment.size() <= code.size() &&
            code.substr(i, lang.singleLineComment.size()) == lang.singleLineComment) {
            if (!currentToken.empty()) {
                segments.push_back(StyledSegment(currentToken, SegmentStyle::CodeBlock));
                currentToken.clear();
            }
            size_t end = code.find(L'\n', i);
            if (end == std::wstring::npos) end = code.size();
            segments.push_back(StyledSegment(code.substr(i, end - i), SegmentStyle::Comment));
            i = end;
            continue;
        }

        // Multi-line comment
        if (!lang.multiLineCommentStart.empty() &&
            i + lang.multiLineCommentStart.size() <= code.size() &&
            code.substr(i, lang.multiLineCommentStart.size()) == lang.multiLineCommentStart) {
            if (!currentToken.empty()) {
                segments.push_back(StyledSegment(currentToken, SegmentStyle::CodeBlock));
                currentToken.clear();
            }
            size_t end = code.find(lang.multiLineCommentEnd, i + lang.multiLineCommentStart.size());
            if (end == std::wstring::npos) end = code.size();
            else end += lang.multiLineCommentEnd.size();
            segments.push_back(StyledSegment(code.substr(i, end - i), SegmentStyle::Comment));
            i = end;
            continue;
        }

        // String literals
        if (code[i] == L'"' || code[i] == L'\'' || code[i] == L'`') {
            if (!currentToken.empty()) {
                segments.push_back(StyledSegment(currentToken, SegmentStyle::CodeBlock));
                currentToken.clear();
            }
            wchar_t quote = code[i];
            size_t start = i;
            ++i;
            while (i < code.size() && code[i] != quote) {
                if (code[i] == L'\\' && i + 1 < code.size()) ++i;
                ++i;
            }
            if (i < code.size()) ++i;
            segments.push_back(StyledSegment(code.substr(start, i - start), SegmentStyle::StringType));
            continue;
        }

        // Numbers
        if (IsDigitChar(code[i]) || (code[i] == L'.' && i + 1 < code.size() && IsDigitChar(code[i + 1]))) {
            if (!currentToken.empty()) {
                segments.push_back(StyledSegment(currentToken, SegmentStyle::CodeBlock));
                currentToken.clear();
            }
            size_t start = i;
            while (i < code.size() && (IsDigitChar(code[i]) || code[i] == L'.' || code[i] == L'x' || code[i] == L'X' ||
                                        (code[i] >= L'a' && code[i] <= L'f') || (code[i] >= L'A' && code[i] <= L'F'))) {
                ++i;
            }
            segments.push_back(StyledSegment(code.substr(start, i - start), SegmentStyle::Number));
            continue;
        }

        // Identifiers (keywords, types, functions)
        if (IsIdentChar(code[i]) && !IsDigitChar(code[i])) {
            if (!currentToken.empty()) {
                segments.push_back(StyledSegment(currentToken, SegmentStyle::CodeBlock));
                currentToken.clear();
            }
            size_t start = i;
            while (i < code.size() && IsIdentChar(code[i])) {
                ++i;
            }
            std::wstring word = code.substr(start, i - start);
            std::wstring lowerWord = ToLower(word);

            if (lang.keywords.count(word) || lang.keywords.count(lowerWord)) {
                segments.push_back(StyledSegment(word, SegmentStyle::Keyword));
            } else if (lang.types.count(word) || lang.types.count(lowerWord)) {
                segments.push_back(StyledSegment(word, SegmentStyle::Type));
            } else if (i < code.size() && code[i] == L'(') {
                segments.push_back(StyledSegment(word, SegmentStyle::Function));
            } else {
                segments.push_back(StyledSegment(word, SegmentStyle::CodeBlock));
            }
            continue;
        }

        // Operators
        if (code[i] == L'+' || code[i] == L'-' || code[i] == L'*' || code[i] == L'/' ||
            code[i] == L'=' || code[i] == L'<' || code[i] == L'>' || code[i] == L'!' ||
            code[i] == L'&' || code[i] == L'|' || code[i] == L'^' || code[i] == L'%' ||
            code[i] == L'~' || code[i] == L'?' || code[i] == L':') {
            if (!currentToken.empty()) {
                segments.push_back(StyledSegment(currentToken, SegmentStyle::CodeBlock));
                currentToken.clear();
            }
            segments.push_back(StyledSegment(std::wstring(1, code[i]), SegmentStyle::Operator));
            ++i;
            continue;
        }

        // Other characters
        currentToken += code[i];
        ++i;
    }

    if (!currentToken.empty()) {
        segments.push_back(StyledSegment(currentToken, SegmentStyle::CodeBlock));
    }

    if (segments.empty()) {
        segments.push_back(StyledSegment(code, SegmentStyle::CodeBlock));
    }

    return segments;
}

std::vector<StyledLine> MarkdownParser::Parse(const std::vector<std::wstring>& lines) {
    std::vector<StyledLine> result;
    inCodeBlock_ = false;
    currentCodeLanguage_.clear();

    for (const auto& line : lines) {
        LineStyle style = DetectLineStyle(line);
        std::wstring strippedText = StripMarkdownSyntax(line, style);

        int indent = 0;
        if (style == LineStyle::Blockquote) {
            std::wstring trimmed = Trim(line);
            indent = 0;
            for (size_t i = 0; i < trimmed.size(); ++i) {
                if (trimmed[i] == L'>') {
                    ++indent;
                    if (i + 1 < trimmed.size() && trimmed[i + 1] == L' ')
                        ++i;
                } else if (trimmed[i] == L' ') {
                    // skip
                } else {
                    break;
                }
            }
        }

        StyledLine styledLine;
        styledLine.style = style;
        styledLine.indent = indent;
        styledLine.fence = fenceKind_;
        styledLine.codeLanguage = currentCodeLanguage_;
        if (fenceKind_ == 2)
            currentCodeLanguage_.clear();

        if (fenceKind_ != 0) {
            styledLine.segments.clear();
        } else if (style == LineStyle::CodeBlock && inCodeBlock_) {
            styledLine.segments = HighlightCode(strippedText, currentCodeLanguage_);
        } else {
            styledLine.segments = ParseInlineFormatting(strippedText, style);
        }

        if (style == LineStyle::OrderedList) {
            std::wstring trimmed = Trim(line);
            size_t dot = trimmed.find(L'.');
            if (dot != std::wstring::npos && dot > 0 && dot < 8)
                styledLine.segments.insert(styledLine.segments.begin(),
                    StyledSegment(trimmed.substr(0, dot + 1) + L" ", SegmentStyle::ListMarker));
        }

        if (style == LineStyle::UnorderedList && !styledLine.segments.empty()) {
            auto& text = styledLine.segments[0].text;
            if (text.size() >= 4 && text[0] == L'[' && text[2] == L']' && text[3] == L' ') {
                const bool done = text[1] == L'x' || text[1] == L'X';
                if (done || text[1] == L' ') {
                    styledLine.segments[0].text = text.substr(4);
                    styledLine.segments.insert(styledLine.segments.begin(),
                        StyledSegment(done ? L"☑ " : L"☐ ", SegmentStyle::ListMarker));
                }
            }
        }

        result.push_back(styledLine);
    }

    return result;
}
