#include "html_converter.hpp"
#include <algorithm>
#include <regex>
#include <sstream>

std::string htmlToPlainText(const std::string& html) {
    std::string result = html;

    // Удалить script блоки (ручная обработка multiline)
    auto removeBlock = [](std::string& s, const std::string& openTag, const std::string& closeTag) {
        while (true) {
            auto start = s.find(openTag);
            if (start == std::string::npos) break;
            auto end = s.find(closeTag, start);
            if (end == std::string::npos) {
                s.erase(start);
                break;
            }
            s.erase(start, end - start + closeTag.size());
        }
    };

    removeBlock(result, "<script", "</script>");
    removeBlock(result, "<style", "</style>");

    // Заменить <br> и <p> на переводы строк
    std::regex brRegex("<br\\s*/?>", std::regex::icase);
    result = std::regex_replace(result, brRegex, "\n");

    std::regex pRegex("<p[^>]*>", std::regex::icase);
    result = std::regex_replace(result, pRegex, "\n\n");

    // Заменить заголовки на переводы строк
    std::regex headerRegex("<h[1-6][^>]*>", std::regex::icase);
    result = std::regex_replace(result, headerRegex, "\n\n");

    std::regex headerCloseRegex("</h[1-6]>", std::regex::icase);
    result = std::regex_replace(result, headerCloseRegex, "\n");

    // Заменить <li> на маркеры
    std::regex liRegex("<li[^>]*>", std::regex::icase);
    result = std::regex_replace(result, liRegex, "\n  * ");

    // Удалить все остальные теги
    std::regex tagRegex("<[^>]+>");
    result = std::regex_replace(result, tagRegex, "");

    // Декодировать HTML entities
    std::regex ampRegex("&amp;", std::regex::icase);
    result = std::regex_replace(result, ampRegex, "&");

    std::regex ltRegex("&lt;", std::regex::icase);
    result = std::regex_replace(result, ltRegex, "<");

    std::regex gtRegex("&gt;", std::regex::icase);
    result = std::regex_replace(result, gtRegex, ">");

    std::regex quotRegex("&quot;", std::regex::icase);
    result = std::regex_replace(result, quotRegex, "\"");

    std::regex nbspRegex("&nbsp;", std::regex::icase);
    result = std::regex_replace(result, nbspRegex, " ");

    std::regex aposRegex("&apos;", std::regex::icase);
    result = std::regex_replace(result, aposRegex, "'");

    // Удалить множественные переводы строк
    std::regex multiNewlineRegex("\n{3,}");
    result = std::regex_replace(result, multiNewlineRegex, "\n\n");

    // Trim
    auto start = result.find_first_not_of(" \t\n\r");
    auto end = result.find_last_not_of(" \t\n\r");
    if (start != std::string::npos && end != std::string::npos) {
        result = result.substr(start, end - start + 1);
    }

    return result;
}
