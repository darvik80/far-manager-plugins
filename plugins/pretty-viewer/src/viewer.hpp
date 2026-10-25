#ifndef PRETTY_VIEWER_HPP_
#define PRETTY_VIEWER_HPP_

#include "far/plugin.hpp"
#include "format.hpp"
#include <string>

void ShowPrettyViewer(const PluginStartupInfo& info, ViewDoc doc, std::vector<ViewLine> raw, const std::wstring& filename);

#endif
