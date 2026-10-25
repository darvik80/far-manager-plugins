#ifndef PRETTY_FORMAT_HPP_
#define PRETTY_FORMAT_HPP_

#include <string>
#include <vector>

enum class Token : uint8_t {
    Punct,
    Key,
    String,
    Number,
    Bool,
    Null,
    Comment,
    Tag,
    Attr,
    Text,
    Error,
};

struct Span {
    Token token;
    std::wstring text;
};

struct ViewLine {
    std::vector<Span> spans;
};

struct ViewDoc {
    std::wstring format;
    std::wstring error;
    std::vector<ViewLine> lines;
};

ViewDoc FormatDocument(const std::wstring& text, const std::wstring& extension);
std::wstring JoinLine(const ViewLine& line);
std::vector<ViewLine> SplitRaw(const std::wstring& text);

#endif
