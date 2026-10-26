#ifndef HTML_CONVERTER_HPP_
#define HTML_CONVERTER_HPP_

#include <string>

std::string htmlToPlainText(const std::string& html);
std::string normalizeMailText(const std::string& text);
size_t visibleMailCount(const std::string& text);

#endif
