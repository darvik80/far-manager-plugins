#include "viewer.hpp"
#include "guid.hpp"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace {

struct Theme {
    const wchar_t* name;
    COLORREF bg, bgCur, fg, muted;
    COLORREF key, str, num, boolean, nullc, comment, tag, attr, punct, error, search;
};

const Theme kThemes[] = {
    { L"Far",
      RGB(0x00, 0x00, 0x80), RGB(0x00, 0x00, 0xA0), RGB(0xC0, 0xC0, 0xC0), RGB(0x80, 0x80, 0x80),
      RGB(0xFF, 0xFF, 0x00), RGB(0x00, 0xFF, 0x00), RGB(0x00, 0xFF, 0xFF), RGB(0xFF, 0x00, 0xFF),
      RGB(0x80, 0x80, 0x80), RGB(0x80, 0x80, 0x80), RGB(0xFF, 0xFF, 0x55), RGB(0x55, 0xFF, 0xFF),
      RGB(0xFF, 0xFF, 0xFF), RGB(0xFF, 0x55, 0x55), RGB(0xFF, 0xFF, 0x00) },
    { L"Midnight",
      RGB(0x00, 0x00, 0x80), RGB(0x00, 0x00, 0xA8), RGB(0xFF, 0xFF, 0xFF), RGB(0xA0, 0xA0, 0xA0),
      RGB(0xFF, 0xFF, 0x55), RGB(0x55, 0xFF, 0x55), RGB(0x55, 0xFF, 0xFF), RGB(0xFF, 0x55, 0xFF),
      RGB(0xA0, 0xA0, 0xA0), RGB(0xA0, 0xA0, 0xA0), RGB(0x55, 0xFF, 0xFF), RGB(0xFF, 0xFF, 0x55),
      RGB(0xFF, 0xFF, 0xFF), RGB(0xFF, 0x80, 0x80), RGB(0xFF, 0xFF, 0x00) },
    { L"Darcula",
      RGB(0x2B, 0x2B, 0x2B), RGB(0x32, 0x32, 0x32), RGB(0xA9, 0xB7, 0xC6), RGB(0x80, 0x80, 0x80),
      RGB(0xCC, 0x78, 0x32), RGB(0x6A, 0x87, 0x59), RGB(0x68, 0x97, 0xBB), RGB(0xCC, 0x78, 0x32),
      RGB(0x80, 0x80, 0x80), RGB(0x80, 0x80, 0x80), RGB(0xFF, 0xC6, 0x6D), RGB(0x98, 0x76, 0xAA),
      RGB(0xA9, 0xB7, 0xC6), RGB(0xFF, 0x6B, 0x68), RGB(0x21, 0x42, 0x83) },
    { L"Light",
      RGB(0xFF, 0xFF, 0xFF), RGB(0xE4, 0xEE, 0xF8), RGB(0x08, 0x08, 0x08), RGB(0x80, 0x80, 0x80),
      RGB(0x00, 0x33, 0xB3), RGB(0x06, 0x7D, 0x17), RGB(0x17, 0x50, 0xEB), RGB(0x9E, 0x5B, 0x00),
      RGB(0x80, 0x80, 0x80), RGB(0x8C, 0x8C, 0x8C), RGB(0x00, 0x62, 0x7A), RGB(0x00, 0x33, 0xB3),
      RGB(0x08, 0x08, 0x08), RGB(0xC0, 0x00, 0x00), RGB(0xFF, 0xE8, 0xA3) },
};

int g_theme = 0;

const Theme& Th() { return kThemes[g_theme]; }

FarColor Paint(COLORREF fg, COLORREF bg) {
    FarColor color;
    color.Flags = 0;
    color.ForegroundColor = fg;
    color.BackgroundColor = bg;
    return color;
}

COLORREF ColorOf(Token token) {
    const Theme& t = Th();
    switch (token) {
        case Token::Key: return t.key;
        case Token::String: return t.str;
        case Token::Number: return t.num;
        case Token::Bool: return t.boolean;
        case Token::Null: return t.nullc;
        case Token::Comment: return t.comment;
        case Token::Tag: return t.tag;
        case Token::Attr: return t.attr;
        case Token::Error: return t.error;
        case Token::Punct: return t.punct;
        case Token::Text: return t.fg;
    }
    return t.fg;
}

struct State {
    const PluginStartupInfo* info = nullptr;
    std::vector<ViewLine> pretty;
    std::vector<ViewLine> raw;
    std::wstring format;
    std::wstring title;
    std::wstring status;
    std::wstring query;
    bool rawMode = false;
    std::vector<FAR_CHAR_INFO> buffer;
    int width = 0;
    int height = 0;
    size_t top = 0;
    size_t current = 0;
    int hscroll = 0;

    const std::vector<ViewLine>& Lines() const { return rawMode ? raw : pretty; }
};

State* g_state = nullptr;

constexpr int kMaxDlgW = 400;
constexpr int kMaxDlgH = 200;

void Clamp(State* state) {
    const auto& lines = state->Lines();
    if (lines.empty()) {
        state->top = state->current = 0;
        return;
    }
    if (state->current >= lines.size()) state->current = lines.size() - 1;
    if (state->top > state->current) state->top = state->current;
    if (state->height > 0 && state->current >= state->top + static_cast<size_t>(state->height))
        state->top = state->current - static_cast<size_t>(state->height) + 1;
}

void Render(State* state) {
    const auto& lines = state->Lines();
    const int width = state->width;
    const int height = state->height;
    if (width <= 0 || height <= 0) return;
    const size_t cells = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (cells > state->buffer.size()) return;
    const FarColor fill = Paint(Th().fg, Th().bg);
    for (auto& cell : state->buffer) {
        cell.Char = L' ';
        cell.Attributes = fill;
    }
    std::wstring needle;
    for (wchar_t ch : state->query) needle.push_back(towlower(ch));

    for (int row = 0; row < height; ++row) {
        const size_t index = state->top + static_cast<size_t>(row);
        if (index >= lines.size()) break;
        FAR_CHAR_INFO* dest = state->buffer.data() + static_cast<size_t>(row) * width;
        const bool current = index == state->current;
        const COLORREF bg = current ? Th().bgCur : Th().bg;
        for (int x = 0; x < width; ++x) dest[x].Attributes = Paint(Th().fg, bg);
        if (current) {
            dest[0].Char = L'>';
            dest[0].Attributes = Paint(Th().key, bg);
        }
        std::wstring text = JoinLine(lines[index]);
        std::wstring folded;
        folded.reserve(text.size());
        for (wchar_t ch : text) folded.push_back(towlower(ch));
        std::vector<int> hit(text.size(), 0);
        if (!needle.empty()) {
            size_t pos = 0;
            while ((pos = folded.find(needle, pos)) != std::wstring::npos) {
                for (size_t k = 0; k < needle.size() && pos + k < hit.size(); ++k) hit[pos + k] = 1;
                pos += needle.size();
            }
        }
        int col = 0;
        int x = 1;
        size_t charIndex = 0;
        for (const auto& span : lines[index].spans) {
            const FarColor color = Paint(ColorOf(span.token), bg);
            for (wchar_t ch : span.text) {
                const bool marked = charIndex < hit.size() && hit[charIndex];
                ++charIndex;
                if (col++ < state->hscroll) continue;
                if (x >= width) break;
                dest[x].Char = ch;
                dest[x].Attributes = marked ? Paint(Th().fg, Th().search) : color;
                ++x;
            }
        }
    }
}

void UpdateStatus(State* state, HANDLE dlg) {
    const size_t total = state->Lines().size();
    const size_t current = total == 0 ? 0 : state->current + 1;
    state->status = L" " + std::to_wstring(current) + L" / " + std::to_wstring(total);
    state->status += L"   " + state->format;
    state->status += L"   ";
    state->status += Th().name;
    if (state->rawMode) state->status += L"   исходный";
    if (!state->query.empty()) state->status += L"   /" + state->query;
    state->status += L"    F2 исходный   F7 поиск   F8 тема   Esc";
    if (dlg && state->info->SendDlgMessage)
        state->info->SendDlgMessage(dlg, DM_SETTEXTPTR, 2, const_cast<wchar_t*>(state->status.c_str()));
}

void Refresh(State* state, HANDLE dlg) {
    Clamp(state);
    Render(state);
    UpdateStatus(state, dlg);
    if (dlg && state->info->SendDlgMessage)
        state->info->SendDlgMessage(dlg, DM_REDRAW, 0, nullptr);
}

void Move(State* state, size_t next, HANDLE dlg) {
    const size_t total = state->Lines().size();
    if (total == 0) next = 0;
    else if (next >= total) next = total - 1;
    state->current = next;
    Refresh(state, dlg);
}

bool Find(State* state, int direction, HANDLE dlg) {
    if (state->query.empty() || state->Lines().empty()) return false;
    std::wstring needle;
    for (wchar_t ch : state->query) needle.push_back(towlower(ch));
    const size_t total = state->Lines().size();
    for (size_t step = 1; step <= total; ++step) {
        size_t index = state->current;
        if (direction >= 0) index = (index + step) % total;
        else index = (index + total - (step % total)) % total;
        std::wstring folded;
        for (wchar_t ch : JoinLine(state->Lines()[index])) folded.push_back(towlower(ch));
        if (folded.find(needle) != std::wstring::npos) {
            Move(state, index, dlg);
            return true;
        }
    }
    return false;
}

void ConsoleDialogSize(const PluginStartupInfo* info, int& dlgW, int& dlgH, SMALL_RECT& rect) {
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
}

void PlaceViewer(State* state, HANDLE dlg, int dlgW, int dlgH, const SMALL_RECT& rect) {
    COORD size{ static_cast<SHORT>(dlgW), static_cast<SHORT>(dlgH) };
    state->info->SendDlgMessage(dlg, DM_RESIZEDIALOG, 0, &size);
    COORD pos{ rect.Left, rect.Top };
    state->info->SendDlgMessage(dlg, DM_MOVEDIALOG, 0, &pos);
    SMALL_RECT box{ 0, 0, static_cast<SHORT>(dlgW - 1), static_cast<SHORT>(dlgH - 1) };
    state->info->SendDlgMessage(dlg, DM_SETITEMPOSITION, 0, &box);
    SMALL_RECT view{ 1, 1, static_cast<SHORT>(dlgW - 2), static_cast<SHORT>(dlgH - 3) };
    state->info->SendDlgMessage(dlg, DM_SETITEMPOSITION, 1, &view);
    SMALL_RECT status{ 2, static_cast<SHORT>(dlgH - 2), static_cast<SHORT>(dlgW - 3), static_cast<SHORT>(dlgH - 2) };
    state->info->SendDlgMessage(dlg, DM_SETITEMPOSITION, 2, &status);
}

void FitConsole(State* state, HANDLE dlg) {
    SMALL_RECT rect{};
    int dlgW = 0;
    int dlgH = 0;
    ConsoleDialogSize(state->info, dlgW, dlgH, rect);
    const int viewW = dlgW - 2;
    const int viewH = dlgH - 3;
    if (viewW < 1 || viewH < 1) return;
    state->width = viewW;
    state->height = viewH;
    Clamp(state);
    Render(state);
    UpdateStatus(state, dlg);
    state->info->SendDlgMessage(dlg, DM_ENABLEREDRAW, 0, nullptr);
    PlaceViewer(state, dlg, dlgW, dlgH, rect);
    state->info->SendDlgMessage(dlg, DM_ENABLEREDRAW, 1, nullptr);
    state->info->SendDlgMessage(dlg, DM_REDRAW, 0, nullptr);
}

intptr_t WINAPI ViewerDlgProc(HANDLE dlg, intptr_t msg, intptr_t param1, void* param2) {
    State* state = g_state;
    if (!state || !state->info || !state->info->DefDlgProc) return 1;
    if (msg == DN_RESIZECONSOLE) {
        FitConsole(state, dlg);
        return 1;
    }
    if (msg == DN_CONTROLINPUT && param2) {
        const auto* rec = static_cast<const INPUT_RECORD*>(param2);
        if (rec->EventType == MOUSE_EVENT && (rec->Event.MouseEvent.dwEventFlags & 0x0004)) {
            const short delta = static_cast<short>(HIWORD(rec->Event.MouseEvent.dwButtonState));
            const size_t step = 3;
            size_t next = state->current;
            if (delta > 0) next = state->current > step ? state->current - step : 0;
            else next = state->current + step;
            Move(state, next, dlg);
            return 1;
        }
        if (rec->EventType == KEY_EVENT && rec->Event.KeyEvent.bKeyDown) {
            const int page = state->height > 0 ? state->height - 1 : 1;
            const DWORD mods = rec->Event.KeyEvent.dwControlKeyState;
            const bool ctrl = (mods & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
            const bool shift = (mods & SHIFT_PRESSED) != 0;
            switch (rec->Event.KeyEvent.wVirtualKeyCode) {
                case VK_ESCAPE:
                case VK_F10:
                case VK_F3:
                    state->info->SendDlgMessage(dlg, DM_CLOSE, -1, nullptr);
                    return 1;
                case VK_UP:
                    Move(state, state->current == 0 ? 0 : state->current - 1, dlg);
                    return 1;
                case VK_DOWN:
                    Move(state, state->current + 1, dlg);
                    return 1;
                case VK_PRIOR:
                    Move(state, state->current > static_cast<size_t>(page) ? state->current - page : 0, dlg);
                    return 1;
                case VK_NEXT:
                    Move(state, state->current + static_cast<size_t>(page), dlg);
                    return 1;
                case VK_HOME:
                    state->hscroll = 0;
                    Move(state, ctrl ? 0 : state->current, dlg);
                    return 1;
                case VK_END:
                    state->hscroll = 0;
                    Move(state, ctrl ? state->Lines().size() : state->current, dlg);
                    return 1;
                case VK_LEFT:
                    state->hscroll = std::max(0, state->hscroll - (ctrl ? 20 : 4));
                    Refresh(state, dlg);
                    return 1;
                case VK_RIGHT:
                    state->hscroll += ctrl ? 20 : 4;
                    Refresh(state, dlg);
                    return 1;
                case VK_F2:
                    state->rawMode = !state->rawMode;
                    state->hscroll = 0;
                    Refresh(state, dlg);
                    return 1;
                case VK_F8: {
                    const int count = static_cast<int>(std::size(kThemes));
                    g_theme = (g_theme + (shift ? count - 1 : 1)) % count;
                    Refresh(state, dlg);
                    return 1;
                }
                case VK_F7: {
                    if (shift && !state->query.empty()) {
                        Find(state, -1, dlg);
                        return 1;
                    }
                    wchar_t src[200];
                    wchar_t dst[200];
                    wcsncpy_s(src, state->query.c_str(), _TRUNCATE);
                    wcscpy_s(dst, src);
                    const std::wstring previous = state->query;
                    if (state->info->InputBox && state->info->InputBox(
                            MainGuid, ViewerDialogGuid, L"Поиск", L"Текст", L"PrettyView.Search",
                            src, dst, std::size(dst), nullptr, 0)) {
                        state->query = dst;
                        if (!state->query.empty() && state->query != previous) Find(state, +1, dlg);
                        Refresh(state, dlg);
                    }
                    return 1;
                }
                default:
                    break;
            }
        }
    }
    if (msg == DN_CTLCOLORDIALOG && param2) {
        *static_cast<FarColor*>(param2) = Paint(Th().fg, Th().bg);
        return 1;
    }
    if (msg == DN_CTLCOLORDLGITEM && param2) {
        auto* colors = static_cast<FarDialogItemColors*>(param2);
        if (colors->Colors && colors->ColorsCount > 0) {
            colors->Colors[0] = Paint(param1 == 2 ? Th().muted : Th().fg, Th().bg);
            if (param1 == 0 && colors->ColorsCount >= 3) {
                colors->Colors[1] = Paint(Th().key, Th().bg);
                colors->Colors[2] = Paint(Th().punct, Th().bg);
            }
            return 1;
        }
    }
    return state->info->DefDlgProc(dlg, msg, param1, param2);
}

}  // namespace

void ShowPrettyViewer(const PluginStartupInfo& info, ViewDoc doc, std::vector<ViewLine> raw, const std::wstring& filename) {
    constexpr int kFarRect = 24;
    SMALL_RECT rect{};
    rect.Right = 79;
    rect.Bottom = 24;
    if (info.AdvControl) info.AdvControl(MainGuid, kFarRect, 0, &rect);
    int dlgW = rect.Right - rect.Left + 1;
    int dlgH = rect.Bottom - rect.Top + 1;
    if (dlgW < 20) dlgW = 20;
    if (dlgH < 8) dlgH = 8;
    if (dlgW > kMaxDlgW) dlgW = kMaxDlgW;
    if (dlgH > kMaxDlgH) dlgH = kMaxDlgH;

    State state;
    state.info = &info;
    state.format = doc.format.empty() ? L"?" : doc.format;
    state.pretty = std::move(doc.lines);
    state.raw = std::move(raw);
    state.title = L" " + state.format + L"  " + filename + L" ";
    state.width = dlgW - 2;
    state.height = dlgH - 3;
    if (state.width < 1) state.width = 1;
    if (state.height < 1) state.height = 1;
    state.buffer.assign(static_cast<size_t>(kMaxDlgW) * kMaxDlgH, FAR_CHAR_INFO{});
    UpdateStatus(&state, nullptr);
    Render(&state);
    g_state = &state;

    FarDialogItem items[3]{};
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
    items[1].Flags = DIF_FOCUS;
    items[1].Param.VBuf = state.buffer.data();

    items[2].Type = DI_TEXT;
    items[2].X1 = 2;
    items[2].Y1 = dlgH - 2;
    items[2].X2 = dlgW - 3;
    items[2].Data = state.status.c_str();

    HANDLE dialog = info.DialogInit(
        MainGuid, ViewerDialogGuid,
        rect.Left, rect.Top, rect.Left + dlgW - 1, rect.Top + dlgH - 1,
        nullptr, items, 3, 0, 0, ViewerDlgProc, 0);
    if (dialog && dialog != INVALID_HANDLE_VALUE) {
        info.DialogRun(dialog);
        info.DialogFree(dialog);
    }
    g_state = nullptr;
}
