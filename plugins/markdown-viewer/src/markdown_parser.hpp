#ifndef MARKDOWN_PARSER_HPP_
#define MARKDOWN_PARSER_HPP_

#include "far/plugin.hpp"
#include <string>
#include <vector>

enum class LineStyle : uint8_t {
    Normal,
    Header1,
    Header2,
    Header3,
    Header4,
    Header5,
    Header6,
    CodeBlock,
    Blockquote,
    UnorderedList,
    OrderedList,
    HorizontalRule,
    Empty,
    Table,
    Image,
};

enum class SegmentStyle : uint8_t {
    Normal,
    Bold,
    Italic,
    Code,
    Strike,
    Link,
    Image,
    Header,
    CodeBlock,
    Blockquote,
    ListMarker,
    HorizontalRule,
    // Syntax highlighting styles
    Keyword,
    StringType,
    Comment,
    Number,
    Preprocessor,
    Function,
    Type,
    Operator,
};

struct StyledSegment {
    std::wstring text;
    SegmentStyle style;
    std::wstring hint;

    StyledSegment() : style(SegmentStyle::Normal) {}
    StyledSegment(const std::wstring& t, SegmentStyle s, const std::wstring& h = L"")
        : text(t), style(s), hint(h) {}
};

struct StyledLine {
    std::vector<StyledSegment> segments;
    LineStyle style;
    int indent;
    int fence;
    std::wstring codeLanguage;

    StyledLine() : style(LineStyle::Normal), indent(0), fence(0) {}
};

class MarkdownParser {
public:
    std::vector<StyledLine> Parse(const std::vector<std::wstring>& lines);

private:
    bool inCodeBlock_ = false;
    int fenceKind_ = 0;
    std::wstring currentCodeLanguage_;

    LineStyle DetectLineStyle(const std::wstring& line);
    std::vector<StyledSegment> ParseInlineFormatting(const std::wstring& text, LineStyle lineStyle);
    std::vector<StyledSegment> HighlightCode(const std::wstring& code, const std::wstring& language);
    std::wstring StripMarkdownSyntax(const std::wstring& line, LineStyle style);
    int CountLeading(const std::wstring& line, wchar_t ch);
    bool IsHorizontalRule(const std::wstring& trimmed);
    bool IsTableSeparator(const std::wstring& trimmed);
};

#endif
