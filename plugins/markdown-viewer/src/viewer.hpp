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
    const std::wstring& filepath);

void ViewerLog(const wchar_t* msg);

#endif
