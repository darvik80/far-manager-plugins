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
    Break,      // <br>: the line is split here, the segment paints nothing
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
    std::wstring anchor;   // explicit id/name, in addition to the slug of a heading

    StyledLine() : style(LineStyle::Normal), indent(0), fence(0) {}
};

class MarkdownParser {
public:
    std::vector<StyledLine> Parse(const std::vector<std::wstring>& lines);

private:
    enum class HtmlPeel {
        None,   // the line is ordinary markdown
        Skip,   // the line belongs to a comment, a script/style body or an empty <pre> opener
        Code,   // 'code' holds one <pre> line
    };

    bool inCodeBlock_ = false;
    int fenceKind_ = 0;
    std::wstring currentCodeLanguage_;

    // Raw HTML that spans lines: <!-- ... --> and <script>/<style>/<textarea> bodies are
    // invisible, <pre> keeps its content literally.
    bool htmlHidden_ = false;
    std::wstring htmlHiddenEnd_;
    bool htmlPre_ = false;
    std::wstring htmlPreLanguage_;
    std::wstring htmlAnchor_;           // id of the heading parsed on this line
    std::wstring htmlAnchorPending_;    // <a name> waiting for the next emitted line

    LineStyle DetectLineStyle(const std::wstring& line);
    std::vector<StyledSegment> ParseInlineFormatting(const std::wstring& text, LineStyle lineStyle);
    std::vector<StyledSegment> HighlightCode(const std::wstring& code, const std::wstring& language);
    std::wstring StripMarkdownSyntax(const std::wstring& line, LineStyle style);
    int CountLeading(const std::wstring& line, wchar_t ch);
    bool IsHorizontalRule(const std::wstring& trimmed);
    bool IsTableSeparator(const std::wstring& trimmed);
    HtmlPeel PeelHtml(const std::wstring& raw, std::wstring& code, std::wstring& tail);
    static void FlushBreaks(std::vector<StyledLine>& result, StyledLine&& line);
};

// Heading anchors follow the slug GitHub generates, so "#some-heading" links resolve.
std::wstring AnchorSlug(const std::wstring& text);
std::vector<std::pair<std::wstring, size_t>> BuildAnchors(const std::vector<StyledLine>& lines);

#endif
