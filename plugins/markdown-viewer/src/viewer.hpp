#ifndef VIEWER_HPP_
#define VIEWER_HPP_

#include "far/plugin.hpp"
#include "markdown_parser.hpp"
#include <string>
#include <vector>

void ShowMarkdownViewer(
    const PluginStartupInfo& info,
    const std::vector<StyledLine>& lines,
    const std::wstring& filename,
    const std::wstring& filepath,
    const std::wstring& anchor = std::wstring());

// Opens the markdown file a link points at, resolved against 'filepath'. Lives in main.cpp
// because it needs the reader and the parser; false when the target is not viewable.
bool OpenLinkedMarkdown(const std::wstring& filepath, const std::wstring& target);

void ViewerLog(const wchar_t* msg);

#endif
