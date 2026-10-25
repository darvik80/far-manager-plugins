#include "viewer.hpp"
#include "guid.hpp"

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
    wchar_t pin = 0;
    FarColor pinColor{};
    std::vector<ViewerCell> cells;
    bool scrollable = false;
};

struct ViewerState {
    const PluginStartupInfo* info = nullptr;
    const std::vector<StyledLine>* lines = nullptr;
    std::wstring title;
    std::wstring status;
    std::wstring query;
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

struct Theme {
    const wchar_t* name;
    COLORREF bg, bgCur, bgCode, bgCodeCur, chip;
    COLORREF fg, strong, border, muted;
    COLORREF keyword, string, number, comment;
    COLORREF function, type, preproc, link, search;
    COLORREF h1, h2, h3, h4, h5, h6;
};

static const Theme kThemes[] = {
    {
        L"Far",
        RGB(0x00, 0x00, 0x80), RGB(0x00, 0x00, 0xA0), RGB(0x00, 0x00, 0x60), RGB(0x00, 0x00, 0x78), RGB(0x00, 0x00, 0xA0),
        RGB(0xC0, 0xC0, 0xC0), RGB(0xFF, 0xFF, 0xFF), RGB(0x00, 0x80, 0x80), RGB(0x80, 0x80, 0x80),
        RGB(0xFF, 0xFF, 0xFF), RGB(0x00, 0xFF, 0x00), RGB(0xFF, 0xFF, 0x00), RGB(0x80, 0x80, 0x80),
        RGB(0xFF, 0xFF, 0x00), RGB(0x00, 0xFF, 0xFF), RGB(0xFF, 0x00, 0xFF), RGB(0x00, 0xFF, 0xFF), RGB(0xFF, 0xFF, 0x00),
        RGB(0xFF, 0xFF, 0x00), RGB(0xFF, 0xFF, 0xFF), RGB(0x00, 0xFF, 0xFF), RGB(0xC0, 0xC0, 0xC0), RGB(0xC0, 0xC0, 0xC0), RGB(0x80, 0x80, 0x80),
    },
    {
        L"Midnight",
        RGB(0x00, 0x00, 0x80), RGB(0x00, 0x00, 0xA8), RGB(0x00, 0x00, 0x58), RGB(0x00, 0x00, 0x70), RGB(0x00, 0x00, 0x98),
        RGB(0xFF, 0xFF, 0xFF), RGB(0xFF, 0xFF, 0xFF), RGB(0x00, 0xAA, 0xAA), RGB(0xA0, 0xA0, 0xA0),
        RGB(0xFF, 0xFF, 0x55), RGB(0x55, 0xFF, 0x55), RGB(0xFF, 0xFF, 0x55), RGB(0xA0, 0xA0, 0xA0),
        RGB(0xFF, 0xFF, 0x55), RGB(0x55, 0xFF, 0xFF), RGB(0xFF, 0x55, 0xFF), RGB(0x55, 0xFF, 0xFF), RGB(0xFF, 0xFF, 0x00),
        RGB(0xFF, 0xFF, 0x55), RGB(0x55, 0xFF, 0xFF), RGB(0xFF, 0xFF, 0xFF), RGB(0xFF, 0xFF, 0xFF), RGB(0xA0, 0xA0, 0xA0), RGB(0xA0, 0xA0, 0xA0),
    },
    {
        L"Darcula",
        RGB(0x2B, 0x2B, 0x2B), RGB(0x32, 0x32, 0x32), RGB(0x24, 0x26, 0x28), RGB(0x2D, 0x30, 0x32), RGB(0x3C, 0x3F, 0x41),
        RGB(0xA9, 0xB7, 0xC6), RGB(0xE8, 0xE8, 0xE8), RGB(0x55, 0x55, 0x55), RGB(0x80, 0x80, 0x80),
        RGB(0xCC, 0x78, 0x32), RGB(0x6A, 0x87, 0x59), RGB(0x68, 0x97, 0xBB), RGB(0x80, 0x80, 0x80),
        RGB(0xFF, 0xC6, 0x6D), RGB(0x98, 0x76, 0xAA), RGB(0xCC, 0x78, 0x32), RGB(0x58, 0x9D, 0xF6), RGB(0x21, 0x42, 0x83),
        RGB(0xFF, 0xC6, 0x6D), RGB(0xCC, 0x78, 0x32), RGB(0xA9, 0xB7, 0xC6), RGB(0xA9, 0xB7, 0xC6), RGB(0x80, 0x80, 0x80), RGB(0x80, 0x80, 0x80),
    },
    {
        L"One Dark",
        RGB(0x28, 0x2C, 0x34), RGB(0x2C, 0x31, 0x3A), RGB(0x21, 0x25, 0x2B), RGB(0x2C, 0x31, 0x3C), RGB(0x2C, 0x31, 0x3A),
        RGB(0xAB, 0xB2, 0xBF), RGB(0xE6, 0xE6, 0xE6), RGB(0x3E, 0x44, 0x51), RGB(0x5C, 0x63, 0x70),
        RGB(0xC6, 0x78, 0xDD), RGB(0x98, 0xC3, 0x79), RGB(0xD1, 0x9A, 0x66), RGB(0x5C, 0x63, 0x70),
        RGB(0x61, 0xAF, 0xEF), RGB(0xE5, 0xC0, 0x7B), RGB(0xE0, 0x6C, 0x75), RGB(0x61, 0xAF, 0xEF), RGB(0x2C, 0x4F, 0x7C),
        RGB(0xE5, 0xC0, 0x7B), RGB(0x61, 0xAF, 0xEF), RGB(0xC6, 0x78, 0xDD), RGB(0xAB, 0xB2, 0xBF), RGB(0xAB, 0xB2, 0xBF), RGB(0x5C, 0x63, 0x70),
    },
    {
        L"Monokai",
        RGB(0x27, 0x28, 0x22), RGB(0x3E, 0x3D, 0x32), RGB(0x1E, 0x1F, 0x1C), RGB(0x3E, 0x3D, 0x32), RGB(0x3E, 0x3D, 0x32),
        RGB(0xF8, 0xF8, 0xF2), RGB(0xFF, 0xFF, 0xFF), RGB(0x49, 0x48, 0x3E), RGB(0x75, 0x71, 0x5E),
        RGB(0xF9, 0x26, 0x72), RGB(0xE6, 0xDB, 0x74), RGB(0xAE, 0x81, 0xFF), RGB(0x75, 0x71, 0x5E),
        RGB(0xA6, 0xE2, 0x2E), RGB(0x66, 0xD9, 0xEF), RGB(0xF9, 0x26, 0x72), RGB(0x66, 0xD9, 0xEF), RGB(0x49, 0x48, 0x3E),
        RGB(0xF9, 0x26, 0x72), RGB(0xE6, 0xDB, 0x74), RGB(0xA6, 0xE2, 0x2E), RGB(0xF8, 0xF8, 0xF2), RGB(0xF8, 0xF8, 0xF2), RGB(0x75, 0x71, 0x5E),
    },
    {
        L"Dracula",
        RGB(0x28, 0x2A, 0x36), RGB(0x44, 0x47, 0x5A), RGB(0x21, 0x22, 0x2C), RGB(0x36, 0x3A, 0x4F), RGB(0x44, 0x47, 0x5A),
        RGB(0xF8, 0xF8, 0xF2), RGB(0xFF, 0xFF, 0xFF), RGB(0x62, 0x72, 0xA4), RGB(0x62, 0x72, 0xA4),
        RGB(0xFF, 0x79, 0xC6), RGB(0xF1, 0xFA, 0x8C), RGB(0xBD, 0x93, 0xF9), RGB(0x62, 0x72, 0xA4),
        RGB(0x50, 0xFA, 0x7B), RGB(0x8B, 0xE9, 0xFD), RGB(0xFF, 0x79, 0xC6), RGB(0x8B, 0xE9, 0xFD), RGB(0x44, 0x47, 0x5A),
        RGB(0xFF, 0x79, 0xC6), RGB(0x8B, 0xE9, 0xFD), RGB(0x50, 0xFA, 0x7B), RGB(0xF8, 0xF8, 0xF2), RGB(0xF8, 0xF8, 0xF2), RGB(0x62, 0x72, 0xA4),
    },
    {
        L"Light",
        RGB(0xFF, 0xFF, 0xFF), RGB(0xE4, 0xEE, 0xF8), RGB(0xF5, 0xF5, 0xF5), RGB(0xE8, 0xEE, 0xF2), RGB(0xEE, 0xEE, 0xEE),
        RGB(0x08, 0x08, 0x08), RGB(0x00, 0x00, 0x00), RGB(0xB0, 0xB0, 0xB0), RGB(0x80, 0x80, 0x80),
        RGB(0x00, 0x33, 0xB3), RGB(0x06, 0x7D, 0x17), RGB(0x17, 0x50, 0xEB), RGB(0x8C, 0x8C, 0x8C),
        RGB(0x00, 0x62, 0x7A), RGB(0x00, 0x00, 0x00), RGB(0x9E, 0x5B, 0x00), RGB(0x17, 0x50, 0xEB), RGB(0xFF, 0xE8, 0xA3),
        RGB(0x00, 0x33, 0xB3), RGB(0x00, 0x62, 0x7A), RGB(0x08, 0x08, 0x08), RGB(0x08, 0x08, 0x08), RGB(0x08, 0x08, 0x08), RGB(0x80, 0x80, 0x80),
    },
    {
        L"Nord",
        RGB(0x2E, 0x34, 0x40), RGB(0x3B, 0x42, 0x52), RGB(0x24, 0x29, 0x33), RGB(0x3B, 0x42, 0x52), RGB(0x3B, 0x42, 0x52),
        RGB(0xD8, 0xDE, 0xE9), RGB(0xEC, 0xEF, 0xF4), RGB(0x4C, 0x56, 0x6A), RGB(0x61, 0x6E, 0x88),
        RGB(0x81, 0xA1, 0xC1), RGB(0xA3, 0xBE, 0x8C), RGB(0xB4, 0x8E, 0xAD), RGB(0x61, 0x6E, 0x88),
        RGB(0x88, 0xC0, 0xD0), RGB(0x8F, 0xBC, 0xBB), RGB(0xBF, 0x61, 0x6A), RGB(0x88, 0xC0, 0xD0), RGB(0x4C, 0x56, 0x6A),
        RGB(0x88, 0xC0, 0xD0), RGB(0x81, 0xA1, 0xC1), RGB(0xD8, 0xDE, 0xE9), RGB(0xD8, 0xDE, 0xE9), RGB(0x61, 0x6E, 0x88), RGB(0x61, 0x6E, 0x88),
    },
    {
        L"Gruvbox",
        RGB(0x28, 0x28, 0x28), RGB(0x3C, 0x38, 0x36), RGB(0x1D, 0x20, 0x21), RGB(0x3C, 0x38, 0x36), RGB(0x3C, 0x38, 0x36),
        RGB(0xEB, 0xDB, 0xB2), RGB(0xFB, 0xF1, 0xC7), RGB(0x50, 0x49, 0x45), RGB(0x92, 0x83, 0x74),
        RGB(0xFB, 0x49, 0x34), RGB(0xB8, 0xBB, 0x26), RGB(0xD3, 0x86, 0x9B), RGB(0x92, 0x83, 0x74),
        RGB(0x83, 0xA5, 0x98), RGB(0xFA, 0xBD, 0x2F), RGB(0xFE, 0x80, 0x19), RGB(0x83, 0xA5, 0x98), RGB(0x66, 0x5C, 0x54),
        RGB(0xFE, 0x80, 0x19), RGB(0xFA, 0xBD, 0x2F), RGB(0x83, 0xA5, 0x98), RGB(0xEB, 0xDB, 0xB2), RGB(0x92, 0x83, 0x74), RGB(0x92, 0x83, 0x74),
    },
    {
        L"Tokyo Night",
        RGB(0x1A, 0x1B, 0x26), RGB(0x29, 0x2E, 0x42), RGB(0x16, 0x16, 0x1E), RGB(0x29, 0x2E, 0x42), RGB(0x29, 0x2E, 0x42),
        RGB(0xC0, 0xCA, 0xF5), RGB(0xC0, 0xCA, 0xF5), RGB(0x3B, 0x42, 0x61), RGB(0x56, 0x5F, 0x89),
        RGB(0xBB, 0x9A, 0xF7), RGB(0x9E, 0xCE, 0x6A), RGB(0xFF, 0x9E, 0x64), RGB(0x56, 0x5F, 0x89),
        RGB(0x7A, 0xA2, 0xF7), RGB(0x2A, 0xC3, 0xDE), RGB(0xF7, 0x76, 0x8E), RGB(0x7A, 0xA2, 0xF7), RGB(0x3D, 0x59, 0xA1),
        RGB(0x7A, 0xA2, 0xF7), RGB(0xBB, 0x9A, 0xF7), RGB(0x2A, 0xC3, 0xDE), RGB(0xC0, 0xCA, 0xF5), RGB(0x56, 0x5F, 0x89), RGB(0x56, 0x5F, 0x89),
    },
    {
        L"Catppuccin",
        RGB(0x1E, 0x1E, 0x2E), RGB(0x31, 0x32, 0x44), RGB(0x18, 0x18, 0x25), RGB(0x31, 0x32, 0x44), RGB(0x31, 0x32, 0x44),
        RGB(0xCD, 0xD6, 0xF4), RGB(0xCD, 0xD6, 0xF4), RGB(0x45, 0x47, 0x5A), RGB(0x6C, 0x70, 0x86),
        RGB(0xCB, 0xA6, 0xF7), RGB(0xA6, 0xE3, 0xA1), RGB(0xFA, 0xB3, 0x87), RGB(0x6C, 0x70, 0x86),
        RGB(0x89, 0xB4, 0xFA), RGB(0xF9, 0xE2, 0xAF), RGB(0xF3, 0x8B, 0xA8), RGB(0x89, 0xB4, 0xFA), RGB(0x45, 0x47, 0x5A),
        RGB(0xCB, 0xA6, 0xF7), RGB(0x89, 0xB4, 0xFA), RGB(0x94, 0xE2, 0xD5), RGB(0xCD, 0xD6, 0xF4), RGB(0x6C, 0x70, 0x86), RGB(0x6C, 0x70, 0x86),
    },
    {
        L"GitHub",
        RGB(0x0D, 0x11, 0x17), RGB(0x16, 0x1B, 0x22), RGB(0x01, 0x04, 0x09), RGB(0x16, 0x1B, 0x22), RGB(0x21, 0x26, 0x2D),
        RGB(0xE6, 0xED, 0xF3), RGB(0xFF, 0xFF, 0xFF), RGB(0x30, 0x36, 0x3D), RGB(0x8B, 0x94, 0x9E),
        RGB(0xFF, 0x7B, 0x72), RGB(0xA5, 0xD6, 0xFF), RGB(0x79, 0xC0, 0xFF), RGB(0x8B, 0x94, 0x9E),
        RGB(0xD2, 0xA8, 0xFF), RGB(0xFF, 0xA6, 0x57), RGB(0xFF, 0x7B, 0x72), RGB(0x58, 0xA6, 0xFF), RGB(0x26, 0x4F, 0x78),
        RGB(0xFF, 0xA6, 0x57), RGB(0x79, 0xC0, 0xFF), RGB(0xD2, 0xA8, 0xFF), RGB(0xE6, 0xED, 0xF3), RGB(0x8B, 0x94, 0x9E), RGB(0x8B, 0x94, 0x9E),
    },
    {
        L"Dark+",
        RGB(0x1E, 0x1E, 0x1E), RGB(0x2A, 0x2D, 0x2E), RGB(0x1A, 0x1A, 0x1A), RGB(0x25, 0x25, 0x26), RGB(0x2D, 0x2D, 0x30),
        RGB(0xD4, 0xD4, 0xD4), RGB(0xFF, 0xFF, 0xFF), RGB(0x3C, 0x3C, 0x3C), RGB(0x80, 0x80, 0x80),
        RGB(0x56, 0x9C, 0xD6), RGB(0xCE, 0x91, 0x78), RGB(0xB5, 0xCE, 0xA8), RGB(0x6A, 0x99, 0x55),
        RGB(0xDC, 0xDC, 0xAA), RGB(0x4E, 0xC9, 0xB0), RGB(0xC5, 0x86, 0xC0), RGB(0x4F, 0xC1, 0xFF), RGB(0x26, 0x4F, 0x78),
        RGB(0x56, 0x9C, 0xD6), RGB(0x4E, 0xC9, 0xB0), RGB(0xDC, 0xDC, 0xAA), RGB(0xD4, 0xD4, 0xD4), RGB(0x80, 0x80, 0x80), RGB(0x80, 0x80, 0x80),
    },
    {
        L"Ayu",
        RGB(0x0B, 0x0E, 0x14), RGB(0x11, 0x15, 0x1C), RGB(0x06, 0x08, 0x0C), RGB(0x11, 0x15, 0x1C), RGB(0x1C, 0x21, 0x2B),
        RGB(0xBF, 0xBD, 0xB6), RGB(0xE6, 0xE1, 0xCF), RGB(0x2D, 0x33, 0x40), RGB(0x5C, 0x67, 0x73),
        RGB(0xFF, 0x8F, 0x40), RGB(0xAA, 0xD9, 0x4C), RGB(0xD2, 0xA6, 0xFF), RGB(0x5C, 0x67, 0x73),
        RGB(0xFF, 0xB4, 0x54), RGB(0x59, 0xC2, 0xFF), RGB(0xF0, 0x71, 0x78), RGB(0x39, 0xBA, 0xE6), RGB(0x1B, 0x3A, 0x4B),
        RGB(0xFF, 0xB4, 0x54), RGB(0xFF, 0x8F, 0x40), RGB(0x59, 0xC2, 0xFF), RGB(0xBF, 0xBD, 0xB6), RGB(0x5C, 0x67, 0x73), RGB(0x5C, 0x67, 0x73),
    },
    {
        L"Solarized",
        RGB(0x00, 0x2B, 0x36), RGB(0x07, 0x36, 0x42), RGB(0x00, 0x1E, 0x26), RGB(0x07, 0x36, 0x42), RGB(0x07, 0x36, 0x42),
        RGB(0x83, 0x94, 0x96), RGB(0xFD, 0xF6, 0xE3), RGB(0x58, 0x6E, 0x75), RGB(0x58, 0x6E, 0x75),
        RGB(0xD3, 0x36, 0x82), RGB(0x2A, 0xA1, 0x98), RGB(0x6C, 0x71, 0xC4), RGB(0x58, 0x6E, 0x75),
        RGB(0x26, 0x8B, 0xD2), RGB(0xB5, 0x89, 0x00), RGB(0xCB, 0x4B, 0x16), RGB(0x26, 0x8B, 0xD2), RGB(0x0D, 0x4F, 0x5E),
        RGB(0x26, 0x8B, 0xD2), RGB(0x2A, 0xA1, 0x98), RGB(0xB5, 0x89, 0x00), RGB(0x83, 0x94, 0x96), RGB(0x58, 0x6E, 0x75), RGB(0x58, 0x6E, 0x75),
    },
    {
        L"Solarized Light",
        RGB(0xFD, 0xF6, 0xE3), RGB(0xEE, 0xE8, 0xD5), RGB(0xEE, 0xE8, 0xD5), RGB(0xE4, 0xDC, 0xC8), RGB(0xEE, 0xE8, 0xD5),
        RGB(0x65, 0x7B, 0x83), RGB(0x07, 0x36, 0x42), RGB(0x93, 0xA1, 0xA1), RGB(0x93, 0xA1, 0xA1),
        RGB(0xD3, 0x36, 0x82), RGB(0x2A, 0xA1, 0x98), RGB(0x6C, 0x71, 0xC4), RGB(0x93, 0xA1, 0xA1),
        RGB(0x26, 0x8B, 0xD2), RGB(0xB5, 0x89, 0x00), RGB(0xCB, 0x4B, 0x16), RGB(0x26, 0x8B, 0xD2), RGB(0xF6, 0xD9, 0x8A),
        RGB(0x26, 0x8B, 0xD2), RGB(0x2A, 0xA1, 0x98), RGB(0xB5, 0x89, 0x00), RGB(0x65, 0x7B, 0x83), RGB(0x93, 0xA1, 0xA1), RGB(0x93, 0xA1, 0xA1),
    },
};

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

static void RenderBuffer(ViewerState* state) {
    if (state->viewWidth <= 0 || state->viewHeight <= 0) return;
    EnsureVisuals(state);
    const int width = state->viewWidth;
    const int height = state->viewHeight;
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
        int x = 1;
        for (int i = origin; i < static_cast<int>(visual.cells.size()) && x < width; ++i, ++x) {
            const ViewerCell& cell = visual.cells[static_cast<size_t>(i)];
            dest[x].Char = cell.ch;
            FarColor color = cell.color;
            color.BackgroundColor = CurrentBg(color.BackgroundColor, current);
            dest[x].Attributes = color;
        }
    }
}

static void UpdateStatus(ViewerState* state, HANDLE hDlg) {
    const size_t total = state->lines ? state->lines->size() : 0;
    const size_t current = total == 0 ? 0 : state->currentLine + 1;
    state->status = L" " + std::to_wstring(current) + L" / " + std::to_wstring(total);
    state->status += L"   " + std::wstring(Th().name);
    if (state->lines && state->currentLine < state->lines->size()) {
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
    if (state->rawMode) state->status += L"   RAW";
    if (!state->query.empty()) state->status += L"   /" + state->query;
    state->status += L"    F7 find    F8 theme    Ctrl+\x2191\x2193 headings    Esc";
    if (hDlg)
        state->info->SendDlgMessage(hDlg, DM_SETTEXTPTR, 2, const_cast<wchar_t*>(state->status.c_str()));
}

static void MoveVisual(ViewerState* state, size_t next, HANDLE hDlg) {
    const bool rebuild = state->builtWidth != state->viewWidth || state->builtRaw != state->rawMode || state->visuals.empty();
    EnsureVisuals(state);
    if (state->visuals.empty()) return;
    if (!rebuild) {
        if (next >= state->visuals.size()) next = state->visuals.size() - 1;
        state->currentVisual = next;
    }
    state->currentLine = state->visuals[state->currentVisual].source;
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
    EnsureVisuals(state);
    for (size_t i = 0; i < state->visuals.size(); ++i) {
        if (state->visuals[i].source == source) {
            MoveVisual(state, i, hDlg);
            return;
        }
    }
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

static intptr_t WINAPI ViewerDlgProc(HANDLE hDlg, intptr_t Msg, intptr_t Param1, void* Param2) {
    ViewerState* state = g_viewerState;
    if (!state || !state->info || !state->info->DefDlgProc)
        return 0;

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
                    MoveVisual(state, state->currentVisual > static_cast<size_t>(page)
                        ? state->currentVisual - page : 0, hDlg);
                    return 1;
                case VK_NEXT:
                    MoveVisual(state, state->currentVisual + static_cast<size_t>(page), hDlg);
                    return 1;
                case VK_LEFT:
                    if (!state->visuals.empty() && state->visuals[state->currentVisual].scrollable)
                        state->hScroll = std::max(0, state->hScroll - (ctrl ? 20 : 4));
                    MoveVisual(state, state->currentVisual, hDlg);
                    return 1;
                case VK_RIGHT:
                    if (!state->visuals.empty() && state->visuals[state->currentVisual].scrollable)
                        state->hScroll += ctrl ? 20 : 4;
                    MoveVisual(state, state->currentVisual, hDlg);
                    return 1;
                case VK_HOME:
                    if (ctrl || state->hScroll == 0) {
                        state->hScroll = 0;
                        MoveVisual(state, 0, hDlg);
                    } else {
                        state->hScroll = 0;
                        MoveVisual(state, state->currentVisual, hDlg);
                    }
                    return 1;
                case VK_END:
                    state->hScroll = 0;
                    MoveVisual(state, state->visuals.empty() ? 0 : state->visuals.size() - 1, hDlg);
                    return 1;
                case VK_F2:
                    state->rawMode = !state->rawMode;
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
    const std::wstring& /*filepath*/)
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

    const int viewWidth = dlgW - 2;
    const int viewHeight = dlgH - 3;

    ViewerState state;
    state.info = &info;
    state.lines = &lines;
    state.title = L" " + filename + L" ";
    state.topLine = 0;
    state.currentLine = 0;
    state.viewWidth = viewWidth;
    state.viewHeight = viewHeight;
    state.rawMode = false;
    state.buffer.assign(static_cast<size_t>(viewWidth) * viewHeight, FAR_CHAR_INFO{});
    UpdateStatus(&state, nullptr);
    RenderBuffer(&state);

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
        info.DialogRun(hDlg);
        info.DialogFree(hDlg);
    }

    g_viewerState = nullptr;
}
