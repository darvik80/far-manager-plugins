#include "viewer.hpp"
#include "guid.hpp"
#include "render/themes.hpp"

#include <algorithm>
#include <cwctype>
#include <vector>
#include <string>
#include <cstdio>
#include <windows.h>

void ViewerLog(const wchar_t* msg) {
    wchar_t tempPath[MAX_PATH];
    if (!GetEnvironmentVariableW(L"TEMP", tempPath, MAX_PATH)) return;
    std::wstring path = std::wstring(tempPath) + L"\\mdviewer_log.txt";
    FILE* f = _wfopen(path.c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"[%llu] %s\n", (unsigned long long)GetTickCount64(), msg);
    fclose(f);
}

struct ViewerCell {
    wchar_t ch = L' ';
    FarColor color{};
};

struct ViewerRow {
    size_t source = 0;
    size_t origin = 0;
    wchar_t pin = 0;
    FarColor pinColor{};
    std::vector<ViewerCell> cells;
    bool scrollable = false;
};

// A caret between two cells of a parsed line: re-wrapping the text keeps the selection,
// because neither the source line nor the cell offsets depend on the console width.
struct SelPoint {
    size_t source = 0;
    size_t cell = 0;
};

static bool Before(const SelPoint& a, const SelPoint& b) {
    return a.source < b.source || (a.source == b.source && a.cell < b.cell);
}

struct ViewerState {
    const PluginStartupInfo* info = nullptr;
    const std::vector<StyledLine>* lines = nullptr;
    std::wstring title;
    std::wstring status;
    std::wstring query;
    std::wstring filepath;
    std::wstring note;
    std::vector<std::pair<std::wstring, size_t>> anchors;
    std::vector<size_t> history;
    size_t linkPick = 0;
    bool hasSel = false;
    SelPoint selAnchor;
    SelPoint selCaret;
    bool dragging = false;
    bool dragged = false;
    std::vector<FAR_CHAR_INFO> buffer;
    std::vector<ViewerRow> visuals;
    size_t topLine = 0;
    size_t currentLine = 0;
    size_t topVisual = 0;
    size_t currentVisual = 0;
    int hScroll = 0;
    int builtWidth = -1;
    bool builtRaw = false;
    int viewWidth = 0;
    int viewHeight = 0;
    bool rawMode = false;
};

static ViewerState* g_viewerState = nullptr;

constexpr int kMaxDlgW = 400;
constexpr int kMaxDlgH = 200;

static int g_theme = 0;

static const Theme& Th() {
    return kThemes[g_theme];
}

static void CycleTheme(int direction) {
    const int count = static_cast<int>(std::size(kThemes));
    g_theme = (g_theme + (direction >= 0 ? 1 : count - 1)) % count;
}

static FarColor Paint(COLORREF fg, COLORREF bg, uint64_t extra = 0) {
    FarColor color;
    color.Flags = extra;
    color.ForegroundColor = fg;
    color.BackgroundColor = bg;
    return color;
}

static FarColor GetSegmentColor(SegmentStyle style, COLORREF bg) {
    const Theme& t = Th();
    switch (style) {
        case SegmentStyle::Bold:         return Paint(t.strong, bg, FCF_FG_BOLD);
        case SegmentStyle::Italic:       return Paint(t.fg, bg, FCF_FG_ITALIC);
        case SegmentStyle::Code:         return Paint(t.string, t.chip);
        case SegmentStyle::Strike:       return Paint(t.muted, bg, FCF_FG_STRIKEOUT);
        case SegmentStyle::Link:         return Paint(t.link, bg, FCF_FG_U_DATA0);
        case SegmentStyle::Image:        return Paint(t.type, bg, FCF_FG_U_DATA0);
        case SegmentStyle::Header:       return Paint(t.h1, bg, FCF_FG_BOLD);
        case SegmentStyle::CodeBlock:    return Paint(t.fg, bg);
        case SegmentStyle::Blockquote:   return Paint(t.muted, bg);
        case SegmentStyle::ListMarker:   return Paint(t.keyword, bg);
        case SegmentStyle::HorizontalRule: return Paint(t.border, bg);
        case SegmentStyle::Keyword:      return Paint(t.keyword, bg, FCF_FG_BOLD);
        case SegmentStyle::StringType:   return Paint(t.string, bg);
        case SegmentStyle::Comment:      return Paint(t.comment, bg, FCF_FG_ITALIC);
        case SegmentStyle::Number:       return Paint(t.number, bg);
        case SegmentStyle::Preprocessor: return Paint(t.preproc, bg);
        case SegmentStyle::Function:     return Paint(t.function, bg);
        case SegmentStyle::Type:         return Paint(t.type, bg);
        case SegmentStyle::Operator:     return Paint(t.fg, bg);
        case SegmentStyle::Normal:
        default:                         return Paint(t.fg, bg);
    }
}

static std::wstring GetPrefix(LineStyle style, int indent) {
    switch (style) {
        case LineStyle::Blockquote: {
            int bars = indent > 0 ? indent : 1;
            std::wstring prefix;
            for (int i = 0; i < bars; ++i)
                prefix += L"│ ";
            return prefix;
        }
        case LineStyle::UnorderedList:
            return L" • ";
        default:
            return L"";
    }
}

static bool IsHeader(LineStyle style) {
    return style >= LineStyle::Header1 && style <= LineStyle::Header6;
}

static FarColor HeaderColor(LineStyle style, COLORREF bg) {
    const Theme& t = Th();
    COLORREF fg = t.h1;
    uint64_t extra = FCF_FG_BOLD;
    switch (style) {
        case LineStyle::Header1: fg = t.h1; break;
        case LineStyle::Header2: fg = t.h2; break;
        case LineStyle::Header3: fg = t.h3; break;
        case LineStyle::Header4: fg = t.h4; break;
        case LineStyle::Header5: fg = t.h5; break;
        case LineStyle::Header6: fg = t.h6; extra = FCF_FG_ITALIC; break;
        default: break;
    }
    return Paint(fg, bg, extra);
}

static COLORREF RowBg(const StyledLine& line, bool current) {
    const Theme& t = Th();
    if (line.fence || line.style == LineStyle::CodeBlock)
        return current ? t.bgCodeCur : t.bgCode;
    return current ? t.bgCur : t.bg;
}

static std::wstring LanguageLabel(const std::wstring& lang) {
    if (lang == L"cpp" || lang == L"c++" || lang == L"cxx" || lang == L"cc" || lang == L"hpp") return L"C++";
    if (lang == L"c" || lang == L"h") return L"C";
    if (lang == L"py" || lang == L"python") return L"Python";
    if (lang == L"js" || lang == L"javascript") return L"JavaScript";
    if (lang == L"ts" || lang == L"typescript") return L"TypeScript";
    if (lang == L"rs" || lang == L"rust") return L"Rust";
    if (lang == L"go" || lang == L"golang") return L"Go";
    if (lang == L"cs" || lang == L"csharp" || lang == L"c#") return L"C#";
    if (lang == L"sh" || lang == L"bash" || lang == L"shell" || lang == L"zsh") return L"Shell";
    if (lang == L"yml" || lang == L"yaml") return L"YAML";
    if (lang == L"rb" || lang == L"ruby") return L"Ruby";
    if (lang.empty()) return L"code";
    std::wstring label = lang;
    label[0] = towupper(label[0]);
    return label;
}

static void ClearRow(FAR_CHAR_INFO* row, int width, FarColor fill) {
    for (int i = 0; i < width; ++i) {
        row[i].Char = L' ';
        row[i].Reserved0 = 0;
        row[i].Reserved1 = 0;
        row[i].Attributes = fill;
    }
}

static void PaintString(FAR_CHAR_INFO* row, int& x, int width, const std::wstring& text, FarColor color) {
    for (wchar_t ch : text) {
        if (x >= width) return;
        if (ch == L'\t') ch = L' ';
        row[x].Char = ch ? ch : L' ';
        row[x].Attributes = color;
        ++x;
    }
}

static void PaintRule(FAR_CHAR_INFO* row, int x, int width, FarColor color) {
    for (int i = x; i < width; ++i) {
        row[i].Char = L'─';
        row[i].Attributes = color;
    }
}

static std::wstring TrimCopy(const std::wstring& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t')) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t')) --b;
    return s.substr(a, b - a);
}

static std::wstring LineText(const StyledLine& line) {
    std::wstring text;
    for (const auto& seg : line.segments) text += seg.text;
    return text;
}

static std::vector<std::wstring> SplitCells(const std::wstring& text) {
    std::vector<std::wstring> cells;
    std::wstring cur;
    for (wchar_t ch : text) {
        if (ch == L'|') {
            cells.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    cells.push_back(cur);
    if (!cells.empty() && TrimCopy(cells.front()).empty()) cells.erase(cells.begin());
    if (!cells.empty() && TrimCopy(cells.back()).empty()) cells.pop_back();
    for (auto& cell : cells) cell = TrimCopy(cell);
    return cells;
}

static bool RowIsSeparator(const std::vector<std::wstring>& cells) {
    if (cells.empty()) return false;
    for (const auto& cell : cells) {
        if (cell.find(L'-') == std::wstring::npos) return false;
        for (wchar_t ch : cell) {
            if (ch != L'-' && ch != L':' && ch != L' ') return false;
        }
    }
    return true;
}

struct TableGrid {
    size_t begin = 0;
    size_t end = 0;
    size_t header = static_cast<size_t>(-1);
    std::vector<size_t> widths;
    std::vector<int> align;
};

static TableGrid MeasureTable(const std::vector<StyledLine>& lines, size_t index) {
    TableGrid grid;
    grid.begin = index;
    while (grid.begin > 0 && lines[grid.begin - 1].style == LineStyle::Table)
        --grid.begin;
    grid.end = index + 1;
    while (grid.end < lines.size() && lines[grid.end].style == LineStyle::Table)
        ++grid.end;

    size_t cols = 0;
    std::vector<std::vector<std::wstring>> rows;
    size_t sep = static_cast<size_t>(-1);
    for (size_t i = grid.begin; i < grid.end; ++i) {
        auto cells = SplitCells(LineText(lines[i]));
        cols = std::max(cols, cells.size());
        if (sep == static_cast<size_t>(-1) && RowIsSeparator(cells))
            sep = i;
        rows.push_back(std::move(cells));
    }
    grid.widths.assign(cols, 1);
    grid.align.assign(cols, 0);
    if (sep != static_cast<size_t>(-1) && sep > grid.begin)
        grid.header = sep - 1;

    for (size_t r = 0; r < rows.size(); ++r) {
        const bool sepRow = RowIsSeparator(rows[r]);
        for (size_t c = 0; c < rows[r].size() && c < cols; ++c) {
            if (sepRow) {
                const auto& spec = rows[r][c];
                const bool left = !spec.empty() && spec.front() == L':';
                const bool right = !spec.empty() && spec.back() == L':';
                grid.align[c] = (left && right) ? 2 : right ? 1 : 0;
            } else {
                grid.widths[c] = std::max(grid.widths[c], rows[r][c].size());
            }
        }
    }
    return grid;
}

static std::wstring FitCell(const std::wstring& text, size_t width, int align) {
    if (text.size() >= width) return text.substr(0, width);
    const size_t gap = width - text.size();
    const size_t left = align == 1 ? gap : align == 2 ? gap / 2 : 0;
    return std::wstring(left, L' ') + text + std::wstring(gap - left, L' ');
}

static void PaintTableEdge(FAR_CHAR_INFO* row, int width, const TableGrid& grid, bool top, bool current) {
    const COLORREF bg = current ? Th().bgCur : Th().bg;
    const FarColor border = Paint(Th().border, bg);
    int x = 1;
    PaintString(row, x, width, top ? L"┌" : L"└", border);
    for (size_t c = 0; c < grid.widths.size(); ++c) {
        PaintString(row, x, width, std::wstring(grid.widths[c] + 2, L'─'), border);
        const bool last = c + 1 == grid.widths.size();
        const wchar_t joint = top ? (last ? L'┐' : L'┬') : (last ? L'┘' : L'┴');
        PaintString(row, x, width, std::wstring(1, joint), border);
    }
}

static void PaintTableLine(FAR_CHAR_INFO* row, int width, const StyledLine& line, size_t index,
                           const std::vector<StyledLine>& lines, bool current) {
    const COLORREF bg = current ? Th().bgCur : Th().bg;
    const TableGrid grid = MeasureTable(lines, index);
    const auto cells = SplitCells(LineText(line));
    const bool sep = RowIsSeparator(cells);
    const bool head = index == grid.header;
    const FarColor border = Paint(Th().border, bg);
    const FarColor text = head ? Paint(Th().h1, bg, FCF_FG_BOLD) : Paint(Th().fg, bg);
    int x = 1;

    if (sep) {
        PaintString(row, x, width, L"├", border);
        for (size_t c = 0; c < grid.widths.size(); ++c) {
            PaintString(row, x, width, std::wstring(grid.widths[c] + 2, L'─'), border);
            const bool last = c + 1 == grid.widths.size();
            PaintString(row, x, width, last ? L"┤" : L"┼", border);
        }
        return;
    }

    PaintString(row, x, width, L"│", border);
    for (size_t c = 0; c < grid.widths.size(); ++c) {
        const std::wstring body = c < cells.size() ? cells[c] : L"";
        const int align = c < grid.align.size() ? grid.align[c] : 0;
        PaintString(row, x, width, L" " + FitCell(body, grid.widths[c], align) + L" ", text);
        PaintString(row, x, width, L"│", border);
    }
}

static void PaintLine(ViewerState* state, FAR_CHAR_INFO* row, int width, size_t lineIndex) {
    const auto& lines = *state->lines;
    const auto& line = lines[lineIndex];
    const bool current = lineIndex == state->currentLine;
    const COLORREF bg = RowBg(line, current);
    const int edge = (state->viewWidth > 0 && state->viewWidth < width) ? state->viewWidth : width;
    ClearRow(row, width, Paint(Th().fg, bg));

    if (current && width > 0) {
        row[0].Char = L'>';
        row[0].Attributes = Paint(Th().h1, bg, FCF_FG_BOLD);
    }

    int x = 1;
    if (state->rawMode) {
        std::wstring text;
        if (line.fence == 1) text = L"```" + line.codeLanguage;
        else if (line.fence == 2) text = L"```";
        else text = LineText(line);
        PaintString(row, x, width, text, Paint(Th().fg, bg));
        return;
    }

    if (line.style == LineStyle::Empty) {
        const bool prevTable = lineIndex > 0 && lines[lineIndex - 1].style == LineStyle::Table;
        const bool nextTable = lineIndex + 1 < lines.size() && lines[lineIndex + 1].style == LineStyle::Table;
        if (nextTable && !prevTable)
            PaintTableEdge(row, width, MeasureTable(lines, lineIndex + 1), true, current);
        else if (prevTable && !nextTable)
            PaintTableEdge(row, width, MeasureTable(lines, lineIndex - 1), false, current);
        return;
    }

    if (line.style == LineStyle::Table) {
        PaintTableLine(row, width, line, lineIndex, lines, current);
        return;
    }

    if (line.fence == 1) {
        if (!current && width > 0) {
            row[0].Char = L'┌';
            row[0].Attributes = Paint(Th().border, bg);
        }
        PaintString(row, x, width, L" " + LanguageLabel(line.codeLanguage) + L" ", Paint(Th().function, bg));
        PaintRule(row, x, edge, Paint(Th().border, bg));
        return;
    }

    if (line.fence == 2 || line.style == LineStyle::HorizontalRule) {
        if (!current && width > 0 && line.fence == 2) {
            row[0].Char = L'└';
            row[0].Attributes = Paint(Th().border, bg);
        }
        PaintRule(row, line.fence == 2 ? x : 1, edge, Paint(Th().border, bg));
        return;
    }

    if (line.style == LineStyle::CodeBlock) {
        if (!current && width > 0) {
            row[0].Char = L'│';
            row[0].Attributes = Paint(Th().border, bg);
        }
        x = 2;
        for (const auto& seg : line.segments) {
            if (x >= width) break;
            PaintString(row, x, width, seg.text, GetSegmentColor(seg.style, bg));
        }
        return;
    }

    const FarColor prefixColor = Paint(line.style == LineStyle::Blockquote ? Th().border : Th().keyword, bg);
    PaintString(row, x, width, GetPrefix(line.style, line.indent), prefixColor);

    const bool header = IsHeader(line.style);
    const FarColor headerColor = HeaderColor(line.style, bg);
    for (const auto& seg : line.segments) {
        if (x >= width) break;
        FarColor color = GetSegmentColor(seg.style, bg);
        if (header && (seg.style == SegmentStyle::Header || seg.style == SegmentStyle::Normal || seg.style == SegmentStyle::Bold))
            color = headerColor;
        PaintString(row, x, width, seg.text, color);
    }

    if (line.style == LineStyle::Header1 && x + 1 < edge) {
        ++x;
        PaintRule(row, x, edge, Paint(Th().border, bg));
    }
}

static bool RowScrolls(const StyledLine& line, size_t index, const std::vector<StyledLine>& lines) {
    if (line.fence || line.style == LineStyle::CodeBlock || line.style == LineStyle::Table ||
        line.style == LineStyle::HorizontalRule)
        return true;
    if (line.style != LineStyle::Empty) return false;
    const bool prev = index > 0 && lines[index - 1].style == LineStyle::Table;
    const bool next = index + 1 < lines.size() && lines[index + 1].style == LineStyle::Table;
    return prev || next;
}

static COLORREF CurrentBg(COLORREF bg, bool current) {
    if (!current) return bg;
    if (bg == Th().bg) return Th().bgCur;
    if (bg == Th().bgCode) return Th().bgCodeCur;
    return bg;
}

static std::vector<ViewerCell> CaptureLine(ViewerState* state, size_t index) {
    constexpr int kLayout = 4096;
    std::vector<FAR_CHAR_INFO> tmp(kLayout);
    const size_t saved = state->currentLine;
    state->currentLine = static_cast<size_t>(-1);
    PaintLine(state, tmp.data(), kLayout, index);
    state->currentLine = saved;

    size_t last = 0;
    for (int i = 0; i < kLayout; ++i) {
        if (tmp[i].Char != L' ') last = static_cast<size_t>(i) + 1;
    }
    std::vector<ViewerCell> cells;
    cells.reserve(last);
    for (size_t i = 0; i < last; ++i) {
        ViewerCell cell;
        cell.ch = tmp[i].Char;
        cell.color = tmp[i].Attributes;
        cells.push_back(cell);
    }
    return cells;
}

static void MarkQuery(std::vector<ViewerCell>& cells, const std::wstring& query) {
    if (query.empty() || cells.empty()) return;
    std::wstring text;
    text.reserve(cells.size());
    for (const auto& cell : cells) text.push_back(towlower(cell.ch));
    std::wstring needle;
    needle.reserve(query.size());
    for (wchar_t ch : query) needle.push_back(towlower(ch));
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::wstring::npos) {
        for (size_t k = 0; k < needle.size() && pos + k < cells.size(); ++k)
            cells[pos + k].color.BackgroundColor = Th().search;
        pos += needle.size();
    }
}

static void AppendVisuals(ViewerState* state, size_t index) {
    const auto& line = (*state->lines)[index];
    auto cells = CaptureLine(state, index);
    MarkQuery(cells, state->query);

    wchar_t pin = 0;
    FarColor pinColor{};
    size_t start = 0;
    if (!cells.empty() && (cells[0].ch == L'│' || cells[0].ch == L'┌' || cells[0].ch == L'└')) {
        pin = cells[0].ch;
        pinColor = cells[0].color;
        start = 1;
    } else if (!cells.empty() && cells[0].ch == L' ') {
        start = 1;
    }

    const bool scrollable = RowScrolls(line, index, *state->lines);
    const int chunk = state->viewWidth > 1 ? state->viewWidth - 1 : 1;

    auto push = [&](size_t from, size_t to, bool first) {
        ViewerRow row;
        row.source = index;
        row.origin = from;
        row.scrollable = scrollable;
        if (first) {
            row.pin = pin;
            row.pinColor = pinColor;
        }
        if (from < cells.size())
            row.cells.assign(cells.begin() + static_cast<std::ptrdiff_t>(from),
                             cells.begin() + static_cast<std::ptrdiff_t>(std::min(to, cells.size())));
        state->visuals.push_back(std::move(row));
    };

    if (start >= cells.size()) {
        push(start, start, true);
        return;
    }

    if (scrollable) {
        push(start, cells.size(), true);
        return;
    }

    size_t i = start;
    bool first = true;
    while (i < cells.size()) {
        size_t take = static_cast<size_t>(chunk);
        if (i + take < cells.size()) {
            size_t br = i + take;
            const size_t minBreak = i + take / 2;
            while (br > minBreak && cells[br - 1].ch != L' ') --br;
            if (br > minBreak && cells[br - 1].ch == L' ')
                take = br - i;
        }
        if (take == 0) take = 1;
        push(i, i + take, first);
        i += take;
        first = false;
    }
}

static void EnsureVisuals(ViewerState* state) {
    if (state->builtWidth == state->viewWidth && state->builtRaw == state->rawMode && !state->visuals.empty())
        return;

    size_t source = state->currentLine;
    int part = 0;
    if (!state->visuals.empty() && state->currentVisual < state->visuals.size()) {
        source = state->visuals[state->currentVisual].source;
        for (size_t i = state->currentVisual; i > 0 && state->visuals[i - 1].source == source; --i)
            ++part;
    }

    state->visuals.clear();
    if (state->lines) {
        for (size_t i = 0; i < state->lines->size(); ++i)
            AppendVisuals(state, i);
    }
    if (state->visuals.empty())
        state->visuals.push_back(ViewerRow{});

    state->builtWidth = state->viewWidth;
    state->builtRaw = state->rawMode;
    state->currentVisual = 0;
    int seen = 0;
    for (size_t i = 0; i < state->visuals.size(); ++i) {
        if (state->visuals[i].source != source) continue;
        state->currentVisual = i;
        if (seen == part) break;
        ++seen;
    }
    state->currentLine = state->visuals[state->currentVisual].source;
    if (state->currentVisual < state->topVisual)
        state->topVisual = state->currentVisual;
    if (state->viewHeight > 0 &&
        state->currentVisual >= state->topVisual + static_cast<size_t>(state->viewHeight))
        state->topVisual = state->currentVisual - static_cast<size_t>(state->viewHeight) + 1;
}

// Cell range of one visual row covered by the selection, indexed into row.cells.
static bool RowSelection(const ViewerState& state, const ViewerRow& row, size_t& lo, size_t& hi) {
    if (!state.hasSel) return false;
    SelPoint from = state.selAnchor;
    SelPoint to = state.selCaret;
    if (Before(to, from))
        std::swap(from, to);
    if (row.source < from.source || row.source > to.source) return false;
    const size_t begin = row.source == from.source ? from.cell : row.origin;
    const size_t end = row.source == to.source ? to.cell : row.origin + row.cells.size();
    lo = begin > row.origin ? begin - row.origin : 0;
    hi = end > row.origin ? end - row.origin : 0;
    if (hi > row.cells.size()) hi = row.cells.size();
    if (lo > hi) lo = hi;
    return lo < hi;
}

static void RenderBuffer(ViewerState* state) {
    if (state->viewWidth <= 0 || state->viewHeight <= 0) return;
    const int width = state->viewWidth;
    const int height = state->viewHeight;
    if (static_cast<size_t>(width) * static_cast<size_t>(height) > state->buffer.size()) return;
    EnsureVisuals(state);
    const FarColor fill = Paint(Th().fg, Th().bg);

    for (int row = 0; row < height; ++row) {
        FAR_CHAR_INFO* dest = state->buffer.data() + static_cast<size_t>(row) * width;
        ClearRow(dest, width, fill);
        const size_t visualIndex = state->topVisual + static_cast<size_t>(row);
        if (visualIndex >= state->visuals.size() || !state->lines || state->lines->empty())
            continue;

        const ViewerRow& visual = state->visuals[visualIndex];
        const bool current = visualIndex == state->currentVisual;
        const StyledLine& line = (*state->lines)[visual.source];
        const COLORREF bg = RowBg(line, current);
        ClearRow(dest, width, Paint(Th().fg, bg));

        if (current) {
            dest[0].Char = L'>';
            dest[0].Attributes = Paint(Th().h1, bg, FCF_FG_BOLD);
        } else if (visual.pin && state->hScroll == 0) {
            dest[0].Char = visual.pin;
            dest[0].Attributes = visual.pinColor;
            dest[0].Attributes.BackgroundColor = CurrentBg(visual.pinColor.BackgroundColor, false);
        }

        const int origin = visual.scrollable ? state->hScroll : 0;
        size_t selLo = 0;
        size_t selHi = 0;
        const bool selected = RowSelection(*state, visual, selLo, selHi);
        int x = 1;
        for (int i = origin; i < static_cast<int>(visual.cells.size()) && x < width; ++i, ++x) {
            const ViewerCell& cell = visual.cells[static_cast<size_t>(i)];
            dest[x].Char = cell.ch;
            FarColor color = cell.color;
            color.BackgroundColor = CurrentBg(color.BackgroundColor, current);
            if (selected && static_cast<size_t>(i) >= selLo && static_cast<size_t>(i) < selHi)
                std::swap(color.ForegroundColor, color.BackgroundColor);
            dest[x].Attributes = color;
        }
    }
}

// Links the viewer can actually follow: a fragment of this file or a path to another one.
static bool IsExternal(const std::wstring& target) {
    return target.find(L"://") != std::wstring::npos || target.rfind(L"mailto:", 0) == 0;
}

static bool Followable(const StyledSegment& seg) {
    if (seg.hint.empty()) return false;
    if (seg.style != SegmentStyle::Link && seg.style != SegmentStyle::Image) return false;
    return seg.hint[0] == L'#' || !IsExternal(seg.hint);
}

static std::vector<const StyledSegment*> LinksOn(ViewerState* state) {
    std::vector<const StyledSegment*> links;
    if (!state->lines || state->currentLine >= state->lines->size()) return links;
    for (const auto& seg : (*state->lines)[state->currentLine].segments)
        if (Followable(seg)) links.push_back(&seg);
    return links;
}

static bool FindAnchor(ViewerState* state, const std::wstring& fragment, size_t& source) {
    const std::wstring needle = AnchorSlug(fragment);
    if (needle.empty()) return false;
    for (const auto& anchor : state->anchors) {
        if (anchor.first == needle) {
            source = anchor.second;
            return true;
        }
    }
    return false;
}

static size_t SelectionSize(ViewerState* state);
static void ClearSel(ViewerState* state);

static void UpdateStatus(ViewerState* state, HANDLE hDlg) {
    const size_t total = state->lines ? state->lines->size() : 0;
    const size_t current = total == 0 ? 0 : state->currentLine + 1;
    state->status = L" " + std::to_wstring(current) + L" / " + std::to_wstring(total);
    state->status += L"   " + std::wstring(Th().name);

    const auto links = LinksOn(state);
    if (!links.empty()) {
        if (state->linkPick >= links.size()) state->linkPick = 0;
        std::wstring target = links[state->linkPick]->hint;
        if (target.size() > 48) target.resize(48);
        state->status += L"   [" + std::to_wstring(state->linkPick + 1) + L"/" +
                         std::to_wstring(links.size()) + L"] " + target;
    } else if (state->lines && state->currentLine < state->lines->size()) {
        std::wstring hints;
        for (const auto& seg : (*state->lines)[state->currentLine].segments) {
            if (seg.hint.empty()) continue;
            if (!hints.empty()) hints += L"  ";
            hints += seg.hint;
            if (hints.size() > 80) break;
        }
        if (hints.size() > 80) hints.resize(80);
        if (!hints.empty()) state->status += L"   " + hints;
    }
    if (!state->note.empty()) state->status += L"   ! " + state->note;
    if (state->rawMode) state->status += L"   RAW";
    if (!state->query.empty()) state->status += L"   /" + state->query;
    const size_t sel = SelectionSize(state);
    if (sel) state->status += L"   sel " + std::to_wstring(sel);
    state->status += L"    F7 find    F8 theme    Ctrl+\x2191\x2193 headings";
    if (!links.empty()) state->status += L"    Enter go    Tab pick";
    if (!state->history.empty()) state->status += L"    Backspace back";
    state->status += sel ? L"    Ctrl+Ins copy" : L"    mouse/Shift select";
    state->status += L"    Esc";
    if (hDlg)
        state->info->SendDlgMessage(hDlg, DM_SETTEXTPTR, 2, const_cast<wchar_t*>(state->status.c_str()));
}

static void MoveVisual(ViewerState* state, size_t next, HANDLE hDlg) {
    const size_t previous = state->currentLine;
    const bool rebuild = state->builtWidth != state->viewWidth || state->builtRaw != state->rawMode || state->visuals.empty();
    EnsureVisuals(state);
    if (state->visuals.empty()) return;
    if (!rebuild) {
        if (next >= state->visuals.size()) next = state->visuals.size() - 1;
        state->currentVisual = next;
    }
    state->currentLine = state->visuals[state->currentVisual].source;
    state->note.clear();
    if (state->currentLine != previous) state->linkPick = 0;
    if (state->currentVisual < state->topVisual)
        state->topVisual = state->currentVisual;
    if (state->viewHeight > 0 &&
        state->currentVisual >= state->topVisual + static_cast<size_t>(state->viewHeight))
        state->topVisual = state->currentVisual - static_cast<size_t>(state->viewHeight) + 1;

    if (state->visuals[state->currentVisual].scrollable) {
        const int chunk = state->viewWidth > 1 ? state->viewWidth - 1 : 1;
        const int maxScroll = std::max(0, static_cast<int>(state->visuals[state->currentVisual].cells.size()) - chunk);
        if (state->hScroll > maxScroll) state->hScroll = maxScroll;
        if (state->hScroll < 0) state->hScroll = 0;
    }

    RenderBuffer(state);
    UpdateStatus(state, hDlg);
    if (hDlg)
        state->info->SendDlgMessage(hDlg, DM_REDRAW, 0, nullptr);
}

static void GoSource(ViewerState* state, size_t source, HANDLE hDlg) {
    ClearSel(state);
    EnsureVisuals(state);
    for (size_t i = 0; i < state->visuals.size(); ++i) {
        if (state->visuals[i].source == source) {
            MoveVisual(state, i, hDlg);
            return;
        }
    }
}

static void Say(ViewerState* state, HANDLE hDlg, const std::wstring& note) {
    state->note = note;
    UpdateStatus(state, hDlg);
    if (hDlg)
        state->info->SendDlgMessage(hDlg, DM_REDRAW, 0, nullptr);
}

static void ClearSel(ViewerState* state) {
    state->hasSel = false;
    state->dragging = false;
}

static bool RowOfPoint(const ViewerState& state, const SelPoint& p, size_t& vIdx, size_t& local) {
    size_t last = static_cast<size_t>(-1);
    for (size_t i = 0; i < state.visuals.size(); ++i) {
        const ViewerRow& row = state.visuals[i];
        if (row.source != p.source) {
            if (last != static_cast<size_t>(-1)) break;
            continue;
        }
        last = i;
        const size_t end = row.origin + row.cells.size();
        if (p.cell >= row.origin && p.cell <= end) {
            vIdx = i;
            local = p.cell - row.origin;
            return true;
        }
    }
    if (last == static_cast<size_t>(-1)) return false;
    vIdx = last;
    local = state.visuals[last].cells.size();
    return true;
}

// Put the selection caret on a visual row column, scrolling the view to keep it visible.
static void SetCaret(ViewerState* state, HANDLE hDlg, size_t vIdx, size_t local, bool resetAnchor) {
    EnsureVisuals(state);
    if (state->visuals.empty()) return;
    if (vIdx >= state->visuals.size()) vIdx = state->visuals.size() - 1;
    const ViewerRow& row = state->visuals[vIdx];
    if (local > row.cells.size()) local = row.cells.size();
    if (row.scrollable) {
        const int chunk = state->viewWidth > 1 ? state->viewWidth - 1 : 1;
        if (static_cast<int>(local) < state->hScroll) state->hScroll = static_cast<int>(local);
        else if (static_cast<int>(local) >= state->hScroll + chunk)
            state->hScroll = static_cast<int>(local) - chunk + 1;
    }
    state->hasSel = true;
    state->selCaret = SelPoint{ row.source, row.origin + local };
    if (resetAnchor) state->selAnchor = state->selCaret;
    MoveVisual(state, vIdx, hDlg);
}

enum class SelMove { Left, Right, Up, Down, RowStart, RowEnd, PageUp, PageDown };

static void ExtendSel(ViewerState* state, HANDLE hDlg, SelMove move) {
    EnsureVisuals(state);
    if (state->visuals.empty()) return;
    size_t v = state->currentVisual;
    size_t local = 0;
    if (!state->hasSel || !RowOfPoint(*state, state->selCaret, v, local)) {
        state->hasSel = true;
        state->selAnchor = SelPoint{ state->visuals[v].source, state->visuals[v].origin + local };
    }
    const int page = state->viewHeight > 0 ? state->viewHeight : 1;
    size_t next = v;
    size_t nextLocal = local;
    switch (move) {
        case SelMove::Left:
            if (nextLocal > 0) --nextLocal;
            else if (next > 0) { --next; nextLocal = state->visuals[next].cells.size(); }
            break;
        case SelMove::Right:
            if (nextLocal < state->visuals[next].cells.size()) ++nextLocal;
            else if (next + 1 < state->visuals.size()) { ++next; nextLocal = 0; }
            break;
        case SelMove::Up:
            if (next > 0) --next;
            break;
        case SelMove::Down:
            if (next + 1 < state->visuals.size()) ++next;
            break;
        case SelMove::PageUp:
            next = next > static_cast<size_t>(page) ? next - page : 0;
            break;
        case SelMove::PageDown:
            next += static_cast<size_t>(page);
            break;
        case SelMove::RowStart:
            nextLocal = 0;
            break;
        case SelMove::RowEnd:
            nextLocal = state->visuals[next].cells.size();
            break;
    }
    SetCaret(state, hDlg, next, nextLocal, false);
}

static size_t SelectionSize(ViewerState* state) {
    if (!state->hasSel) return 0;
    size_t chars = 0;
    size_t lines = 0;
    size_t lastSource = static_cast<size_t>(-1);
    size_t lo = 0;
    size_t hi = 0;
    for (const auto& row : state->visuals) {
        if (!RowSelection(*state, row, lo, hi)) continue;
        chars += hi - lo;
        if (row.source != lastSource) { ++lines; lastSource = row.source; }
    }
    return chars + (lines > 1 ? lines - 1 : 0);
}

static std::wstring CopyableLine(ViewerState* state, size_t source, const std::wstring& text, bool fromStart) {
    std::wstring out = text;
    // The renderer keeps one blank column between the frame and the code; it is not indentation.
    if (fromStart && !out.empty() && out[0] == L' ' && source < state->lines->size() &&
        (*state->lines)[source].style == LineStyle::CodeBlock)
        out.erase(0, 1);
    // Decoration the renderer paints, not text of the document: the rules that head a fence,
    // a table or a heading, and the quote bars on the left of a blockquote.
    size_t b = out.size();
    while (b > 0 && out[b - 1] == L'─') --b;
    out.resize(b);
    if (source < state->lines->size() && (*state->lines)[source].style == LineStyle::Blockquote) {
        size_t a = 0;
        while (a < out.size() && (out[a] == L'│' || out[a] == L' ')) ++a;
        out.erase(0, a);
    }
    return out;
}

static std::wstring SelectionText(ViewerState* state) {
    std::wstring out;
    std::wstring line;
    size_t lastSource = static_cast<size_t>(-1);
    bool fromStart = false;
    size_t lo = 0;
    size_t hi = 0;
    for (size_t i = 0; i < state->visuals.size(); ++i) {
        const ViewerRow& row = state->visuals[i];
        if (!RowSelection(*state, row, lo, hi)) continue;
        if (row.source != lastSource) {
            if (lastSource != static_cast<size_t>(-1)) out += CopyableLine(state, lastSource, line, fromStart) + L"\n";
            lastSource = row.source;
            line.clear();
            fromStart = lo == 0 && (i == 0 || state->visuals[i - 1].source != row.source);
        }
        for (size_t j = lo; j < hi; ++j) line.push_back(row.cells[j].ch);
    }
    if (lastSource != static_cast<size_t>(-1)) out += CopyableLine(state, lastSource, line, fromStart);
    return out;
}

static bool PutClipboard(const std::wstring& text) {
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return false;
    wchar_t* dst = static_cast<wchar_t*>(GlobalLock(mem));
    if (!dst) {
        GlobalFree(mem);
        return false;
    }
    wmemcpy(dst, text.c_str(), text.size() + 1);
    GlobalUnlock(mem);
    if (!OpenClipboard(nullptr)) {
        GlobalFree(mem);
        return false;
    }
    EmptyClipboard();
    const bool ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
    CloseClipboard();
    if (!ok) GlobalFree(mem);
    return ok;
}

static void CopySelection(ViewerState* state, HANDLE hDlg) {
    const std::wstring text = SelectionText(state);
    if (text.empty()) {
        Say(state, hDlg, L"nothing selected");
        return;
    }
    if (!PutClipboard(text)) {
        Say(state, hDlg, L"clipboard unavailable");
        return;
    }
    Say(state, hDlg, L"copied " + std::to_wstring(text.size()));
}

// FAR hands DN_INPUT records in console coordinates, so the item origin comes from the dialog.
// The result is a visual row plus a column inside it, which SetCaret turns into a SelPoint.
static bool PointFromMouse(ViewerState* state, HANDLE hDlg, const MOUSE_EVENT_RECORD& ms,
                           size_t& vIdx, size_t& local) {
    SMALL_RECT dlg{};
    SMALL_RECT item{};
    if (!state->info->SendDlgMessage(hDlg, DM_GETDLGRECT, 0, &dlg)) return false;
    if (!state->info->SendDlgMessage(hDlg, DM_GETITEMPOSITION, 1, &item)) return false;
    const int x = ms.dwMousePosition.X - (dlg.Left + item.Left);
    const int y = ms.dwMousePosition.Y - (dlg.Top + item.Top);
    if (x < 0 || y < 0 || x >= state->viewWidth || y >= state->viewHeight) return false;
    EnsureVisuals(state);
    const size_t v = state->topVisual + static_cast<size_t>(y);
    if (v >= state->visuals.size()) return false;
    const ViewerRow& row = state->visuals[v];
    const int origin = row.scrollable ? state->hScroll : 0;
    size_t col = static_cast<size_t>(x > 1 ? x - 1 : 0);
    if (origin > 0) col += static_cast<size_t>(origin);
    if (col > row.cells.size()) col = row.cells.size();
    vIdx = v;
    local = col;
    return true;
}

static void SelectWord(ViewerState* state, HANDLE hDlg, size_t vIdx, size_t local) {
    EnsureVisuals(state);
    if (vIdx >= state->visuals.size()) return;
    const ViewerRow& row = state->visuals[vIdx];
    if (row.cells.empty()) return;
    auto word = [](wchar_t ch) { return iswalnum(ch) != 0 || ch == L'_' || ch > 0x7F; };
    if (local >= row.cells.size()) local = row.cells.size() - 1;
    if (!word(row.cells[local].ch)) {
        if (local == 0) return;
        --local;
        if (!word(row.cells[local].ch)) return;
    }
    size_t a = local;
    size_t b = local + 1;
    while (a > 0 && word(row.cells[a - 1].ch)) --a;
    while (b < row.cells.size() && word(row.cells[b].ch)) ++b;
    state->hasSel = true;
    state->selAnchor = SelPoint{ row.source, row.origin + a };
    SetCaret(state, hDlg, vIdx, b, false);
}

static bool HandleMouseInput(ViewerState* state, HANDLE hDlg, const INPUT_RECORD* rec) {
    const MOUSE_EVENT_RECORD& ms = rec->Event.MouseEvent;
    const bool left = (ms.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
    if (ms.dwEventFlags == MOUSE_MOVED) {
        // Without swallowing the move Far would start dragging the whole dialog instead.
        if (!state->dragging || !left) return true;
        size_t v = 0;
        size_t local = 0;
        if (!PointFromMouse(state, hDlg, ms, v, local)) return true;
        // While extending, the cell under the cursor belongs to the selection: +1 so a one cell
        // drag picks a character instead of nothing, the way Far's editor counts it.
        state->dragged = true;
        SetCaret(state, hDlg, v, local + 1, false);
        return false;
    }
    if (ms.dwEventFlags != 0 && ms.dwEventFlags != DOUBLE_CLICK) return true;
    size_t v = 0;
    size_t local = 0;
    if (!PointFromMouse(state, hDlg, ms, v, local)) {
        state->dragging = false;
        return true;
    }
    if (!left) {
        if (!state->dragging) return true;
        // Releasing where the press landed stays a click: the cell under the cursor is only
        // included once the pointer has actually been dragged over it.
        state->dragging = false;
        SetCaret(state, hDlg, v, local + (state->dragged ? 1 : 0), false);
        return true;
    }
    if (ms.dwEventFlags == DOUBLE_CLICK) {
        state->dragging = false;
        SelectWord(state, hDlg, v, local);
        return true;
    }
    state->dragging = true;
    state->dragged = false;
    SetCaret(state, hDlg, v, local, true);
    return true;
}

static void FollowLink(ViewerState* state, HANDLE hDlg) {
    std::vector<const StyledSegment*> links = LinksOn(state);
    if (links.empty()) {
        Say(state, hDlg, L"no link on this line");
        return;
    }
    if (state->linkPick >= links.size()) state->linkPick = 0;
    const std::wstring target = links[state->linkPick]->hint;

    if (target[0] == L'#') {
        size_t source = 0;
        if (!FindAnchor(state, target.substr(1), source)) {
            Say(state, hDlg, L"no heading " + target);
            return;
        }
        state->history.push_back(state->currentLine);
        GoSource(state, source, hDlg);
        return;
    }

    if (OpenLinkedMarkdown(state->filepath, target)) {
        // A nested viewer has just closed: the console size may differ and its cells are stale.
        state->builtWidth = -1;
        MoveVisual(state, state->currentVisual, hDlg);
        return;
    }
    Say(state, hDlg, L"cannot open " + target);
}

static void GoBack(ViewerState* state, HANDLE hDlg) {
    if (state->history.empty()) return;
    const size_t source = state->history.back();
    state->history.pop_back();
    GoSource(state, source, hDlg);
}

static std::wstring SearchText(const StyledLine& line) {
    if (line.fence == 1) return L"```" + line.codeLanguage;
    if (line.fence == 2) return L"```";
    std::wstring text;
    for (const auto& seg : line.segments) text += seg.text;
    return text;
}

static void FindText(ViewerState* state, int direction, bool includeCurrent, HANDLE hDlg) {
    if (!state->lines || state->lines->empty() || state->query.empty()) return;
    EnsureVisuals(state);
    std::wstring needle;
    for (wchar_t ch : state->query) needle.push_back(towlower(ch));
    const size_t count = state->lines->size();
    const size_t start = state->currentLine;
    for (size_t step = includeCurrent ? 0 : 1; step < count; ++step) {
        size_t index = start;
        if (direction >= 0) index = (start + step) % count;
        else index = (start + count - (step % count)) % count;
        std::wstring hay = SearchText((*state->lines)[index]);
        for (auto& ch : hay) ch = towlower(ch);
        if (hay.find(needle) != std::wstring::npos) {
            state->builtWidth = -1;
            state->currentLine = index;
            EnsureVisuals(state);
            GoSource(state, index, hDlg);
            return;
        }
    }
    state->builtWidth = -1;
    RenderBuffer(state);
    UpdateStatus(state, hDlg);
    if (hDlg) state->info->SendDlgMessage(hDlg, DM_REDRAW, 0, nullptr);
}

static bool ConsoleDialogSize(const PluginStartupInfo* info, int& dlgW, int& dlgH, SMALL_RECT& rect) {
    rect = {};
    rect.Right = 79;
    rect.Bottom = 24;
    if (info && info->AdvControl)
        info->AdvControl(MainGuid, 24, 0, &rect);
    dlgW = rect.Right - rect.Left + 1;
    dlgH = rect.Bottom - rect.Top + 1;
    if (dlgW < 20) dlgW = 20;
    if (dlgH < 8) dlgH = 8;
    if (dlgW > kMaxDlgW) dlgW = kMaxDlgW;
    if (dlgH > kMaxDlgH) dlgH = kMaxDlgH;
    return true;
}

static void PlaceViewer(ViewerState* state, HANDLE hDlg, int dlgW, int dlgH, const SMALL_RECT& rect) {
    COORD size{ static_cast<SHORT>(dlgW), static_cast<SHORT>(dlgH) };
    state->info->SendDlgMessage(hDlg, DM_RESIZEDIALOG, 0, &size);
    COORD pos{ rect.Left, rect.Top };
    state->info->SendDlgMessage(hDlg, DM_MOVEDIALOG, 0, &pos);
    SMALL_RECT box{ 0, 0, static_cast<SHORT>(dlgW - 1), static_cast<SHORT>(dlgH - 1) };
    state->info->SendDlgMessage(hDlg, DM_SETITEMPOSITION, 0, &box);
    SMALL_RECT view{ 1, 1, static_cast<SHORT>(dlgW - 2), static_cast<SHORT>(dlgH - 3) };
    state->info->SendDlgMessage(hDlg, DM_SETITEMPOSITION, 1, &view);
    SMALL_RECT status{ 2, static_cast<SHORT>(dlgH - 2), static_cast<SHORT>(dlgW - 3), static_cast<SHORT>(dlgH - 2) };
    state->info->SendDlgMessage(hDlg, DM_SETITEMPOSITION, 2, &status);
}

static void FitConsole(ViewerState* state, HANDLE hDlg) {
    SMALL_RECT rect{};
    int dlgW = 0;
    int dlgH = 0;
    ConsoleDialogSize(state->info, dlgW, dlgH, rect);
    const int viewW = dlgW - 2;
    const int viewH = dlgH - 3;
    if (viewW < 1 || viewH < 1) return;
    const bool same = viewW == state->viewWidth && viewH == state->viewHeight;
    state->viewWidth = viewW;
    state->viewHeight = viewH;
    if (!same) state->builtWidth = -1;
    RenderBuffer(state);
    UpdateStatus(state, hDlg);
    state->info->SendDlgMessage(hDlg, DM_ENABLEREDRAW, 0, nullptr);
    PlaceViewer(state, hDlg, dlgW, dlgH, rect);
    state->info->SendDlgMessage(hDlg, DM_ENABLEREDRAW, 1, nullptr);
    state->info->SendDlgMessage(hDlg, DM_REDRAW, 0, nullptr);
}

static intptr_t WINAPI ViewerDlgProc(HANDLE hDlg, intptr_t Msg, intptr_t Param1, void* Param2) {
    ViewerState* state = g_viewerState;
    if (!state || !state->info || !state->info->DefDlgProc)
        return 0;
    if (Msg == DN_RESIZECONSOLE) {
        FitConsole(state, hDlg);
        return 1;
    }

    // DM_SETINPUTNOTIFY routes every raw record here before Far dispatches it, which is
    // the only way to see mouse moves: button events alone cannot drag a selection.
    if (Msg == DN_INPUT && state->lines && Param2) {
        auto* rec = reinterpret_cast<const INPUT_RECORD*>(Param2);
        if (rec->EventType == MOUSE_EVENT) return HandleMouseInput(state, hDlg, rec);
        return 1;
    }

    if (Msg == DN_CONTROLINPUT && state->lines) {
        auto* rec = reinterpret_cast<const INPUT_RECORD*>(Param2);
        if (rec && rec->EventType == MOUSE_EVENT && (rec->Event.MouseEvent.dwEventFlags & 0x0004)) {
            const short delta = static_cast<short>(HIWORD(rec->Event.MouseEvent.dwButtonState));
            EnsureVisuals(state);
            const size_t step = 3;
            size_t next = state->currentVisual;
            if (delta > 0) next = state->currentVisual > step ? state->currentVisual - step : 0;
            else next = state->currentVisual + step;
            MoveVisual(state, next, hDlg);
            return 1;
        }
        if (rec && rec->EventType == KEY_EVENT && rec->Event.KeyEvent.bKeyDown) {
            const int page = state->viewHeight > 0 ? state->viewHeight : 1;
            const DWORD mods = rec->Event.KeyEvent.dwControlKeyState;
            const bool ctrl = (mods & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
            const bool shift = (mods & SHIFT_PRESSED) != 0;
            EnsureVisuals(state);
            switch (rec->Event.KeyEvent.wVirtualKeyCode) {
                case VK_F3:
                    state->info->SendDlgMessage(hDlg, DM_CLOSE, -1, nullptr);
                    return 1;
                case VK_UP:
                    if (shift && !ctrl) { ExtendSel(state, hDlg, SelMove::Up); return 1; }
                    ClearSel(state);
                    if (ctrl) {
                        size_t source = state->currentLine;
                        while (source > 0) {
                            --source;
                            if (IsHeader((*state->lines)[source].style)) break;
                        }
                        GoSource(state, source, hDlg);
                    } else if (state->currentVisual > 0) {
                        MoveVisual(state, state->currentVisual - 1, hDlg);
                    }
                    return 1;
                case VK_DOWN:
                    if (shift && !ctrl) { ExtendSel(state, hDlg, SelMove::Down); return 1; }
                    ClearSel(state);
                    if (ctrl) {
                        size_t source = state->currentLine;
                        while (source + 1 < state->lines->size()) {
                            ++source;
                            if (IsHeader((*state->lines)[source].style)) break;
                        }
                        GoSource(state, source, hDlg);
                    } else {
                        MoveVisual(state, state->currentVisual + 1, hDlg);
                    }
                    return 1;
                case VK_PRIOR:
                    if (shift) { ExtendSel(state, hDlg, SelMove::PageUp); return 1; }
                    ClearSel(state);
                    MoveVisual(state, state->currentVisual > static_cast<size_t>(page)
                        ? state->currentVisual - page : 0, hDlg);
                    return 1;
                case VK_NEXT:
                    if (shift) { ExtendSel(state, hDlg, SelMove::PageDown); return 1; }
                    ClearSel(state);
                    MoveVisual(state, state->currentVisual + static_cast<size_t>(page), hDlg);
                    return 1;
                case VK_LEFT:
                    if (shift && !ctrl) { ExtendSel(state, hDlg, SelMove::Left); return 1; }
                    ClearSel(state);
                    if (!state->visuals.empty() && state->visuals[state->currentVisual].scrollable)
                        state->hScroll = std::max(0, state->hScroll - (ctrl ? 20 : 4));
                    MoveVisual(state, state->currentVisual, hDlg);
                    return 1;
                case VK_RIGHT:
                    if (shift && !ctrl) { ExtendSel(state, hDlg, SelMove::Right); return 1; }
                    ClearSel(state);
                    if (!state->visuals.empty() && state->visuals[state->currentVisual].scrollable)
                        state->hScroll += ctrl ? 20 : 4;
                    MoveVisual(state, state->currentVisual, hDlg);
                    return 1;
                case VK_HOME:
                    if (shift && !ctrl) { ExtendSel(state, hDlg, SelMove::RowStart); return 1; }
                    ClearSel(state);
                    if (ctrl || state->hScroll == 0) {
                        state->hScroll = 0;
                        MoveVisual(state, 0, hDlg);
                    } else {
                        state->hScroll = 0;
                        MoveVisual(state, state->currentVisual, hDlg);
                    }
                    return 1;
                case VK_END:
                    if (shift && !ctrl) { ExtendSel(state, hDlg, SelMove::RowEnd); return 1; }
                    ClearSel(state);
                    state->hScroll = 0;
                    MoveVisual(state, state->visuals.empty() ? 0 : state->visuals.size() - 1, hDlg);
                    return 1;
                case VK_INSERT:
                    if (ctrl || shift) CopySelection(state, hDlg);
                    return 1;
                case 'C':   // winuser.h has no VK_C; the letter virtual keys are the ASCII codes
                    if (!ctrl) break;
                    CopySelection(state, hDlg);
                    return 1;
                case VK_RETURN:
                    FollowLink(state, hDlg);
                    return 1;
                case VK_BACK:
                    GoBack(state, hDlg);
                    return 1;
                case VK_TAB: {
                    const auto links = LinksOn(state);
                    if (links.empty()) break;
                    const size_t count = links.size();
                    const size_t pick = state->linkPick >= count ? 0 : state->linkPick;
                    state->linkPick = shift ? (pick + count - 1) % count : (pick + 1) % count;
                    UpdateStatus(state, hDlg);
                    return 1;
                }
                case VK_F2:
                    state->rawMode = !state->rawMode;
                    ClearSel(state);
                    state->builtWidth = -1;
                    MoveVisual(state, state->currentVisual, hDlg);
                    return 1;
                case VK_F8:
                    CycleTheme(shift ? -1 : +1);
                    state->builtWidth = -1;
                    MoveVisual(state, state->currentVisual, hDlg);
                    return 1;
                case VK_F7: {
                    if (shift && !state->query.empty()) {
                        FindText(state, -1, false, hDlg);
                        return 1;
                    }
                    wchar_t src[200];
                    wchar_t dst[200];
                    wcsncpy_s(src, state->query.c_str(), _TRUNCATE);
                    wcscpy_s(dst, src);
                    const std::wstring previous = state->query;
                    if (state->info->InputBox && state->info->InputBox(
                            MainGuid, ViewerDialogGuid,
                            L"Find", L"Text to find", L"MdView.Search",
                            src, dst, static_cast<size_t>(std::size(dst)), nullptr, 0)) {
                        state->query = dst;
                        state->builtWidth = -1;
                        if (state->query.empty())
                            MoveVisual(state, state->currentVisual, hDlg);
                        else
                            FindText(state, +1, state->query != previous, hDlg);
                    }
                    return 1;
                }
                default:
                    break;
            }
        }
    }

    if (Msg == DN_CTLCOLORDIALOG && Param2) {
        *static_cast<FarColor*>(Param2) = Paint(Th().fg, Th().bg);
        return TRUE;
    }

    if (Msg == DN_CTLCOLORDLGITEM && Param2) {
        auto* colors = static_cast<FarDialogItemColors*>(Param2);
        if (colors->Colors && colors->ColorsCount > 0) {
            if (Param1 == 0 && colors->ColorsCount >= 3) {
                colors->Colors[0] = Paint(Th().fg, Th().bg);
                colors->Colors[1] = Paint(Th().h1, Th().bg, FCF_FG_BOLD);
                colors->Colors[2] = Paint(Th().border, Th().bg);
                return TRUE;
            }
            if (Param1 == 2) {
                colors->Colors[0] = Paint(Th().muted, Th().bg);
                return TRUE;
            }
        }
    }

    // DN_DRAWDIALOG must come back TRUE, otherwise Far skips painting and
    // the modal dialog sits invisible on top of the plugin menu.
    return state->info->DefDlgProc(hDlg, Msg, Param1, Param2);
}

void ShowMarkdownViewer(
    const PluginStartupInfo& info,
    const std::vector<StyledLine>& lines,
    const std::wstring& filename,
    const std::wstring& filepath,
    const std::wstring& anchor)
{
    constexpr int ACTL_GETFARRECT = 24;

    SMALL_RECT farRect{};
    farRect.Right = 79;
    farRect.Bottom = 24;
    if (info.AdvControl)
        info.AdvControl(MainGuid, ACTL_GETFARRECT, 0, &farRect);

    int dlgW = farRect.Right - farRect.Left + 1;
    int dlgH = farRect.Bottom - farRect.Top + 1;
    if (dlgW < 20) dlgW = 20;
    if (dlgH < 8) dlgH = 8;
    if (dlgW > kMaxDlgW) dlgW = kMaxDlgW;
    if (dlgH > kMaxDlgH) dlgH = kMaxDlgH;

    const int viewWidth = dlgW - 2;
    const int viewHeight = dlgH - 3;

    ViewerState state;
    state.info = &info;
    state.lines = &lines;
    state.title = L" " + filename + L" ";
    state.filepath = filepath;
    state.anchors = BuildAnchors(lines);
    state.topLine = 0;
    state.currentLine = 0;
    state.viewWidth = viewWidth;
    state.viewHeight = viewHeight;
    state.rawMode = false;
    state.buffer.assign(static_cast<size_t>(kMaxDlgW) * kMaxDlgH, FAR_CHAR_INFO{});
    if (!anchor.empty()) {
        const std::wstring fragment = anchor[0] == L'#' ? anchor.substr(1) : anchor;
        size_t source = 0;
        if (FindAnchor(&state, fragment, source))
            state.currentLine = source;
    }
    UpdateStatus(&state, nullptr);
    RenderBuffer(&state);

    ViewerState* outer = g_viewerState;
    g_viewerState = &state;

    std::vector<FarDialogItem> items(3);

    items[0].Type = DI_DOUBLEBOX;
    items[0].X1 = 0;
    items[0].Y1 = 0;
    items[0].X2 = dlgW - 1;
    items[0].Y2 = dlgH - 1;
    items[0].Data = state.title.c_str();

    items[1].Type = DI_USERCONTROL;
    items[1].X1 = 1;
    items[1].Y1 = 1;
    items[1].X2 = dlgW - 2;
    items[1].Y2 = dlgH - 3;
    items[1].Param.VBuf = state.buffer.data();

    items[2].Type = DI_TEXT;
    items[2].X1 = 2;
    items[2].Y1 = dlgH - 2;
    items[2].X2 = dlgW - 3;
    items[2].Y2 = dlgH - 2;
    items[2].Data = state.status.c_str();

    HANDLE hDlg = info.DialogInit(
        MainGuid,
        ViewerDialogGuid,
        farRect.Left,
        farRect.Top,
        farRect.Left + dlgW - 1,
        farRect.Top + dlgH - 1,
        nullptr,
        items.data(),
        items.size(),
        0,
        0,
        ViewerDlgProc,
        0);

    if (hDlg && hDlg != INVALID_HANDLE_VALUE) {
        info.SendDlgMessage(hDlg, DM_SETINPUTNOTIFY, 1, nullptr);
        info.DialogRun(hDlg);
        info.DialogFree(hDlg);
    }

    g_viewerState = outer;
}
