#ifndef NK_SETTINGS_HPP_
#define NK_SETTINGS_HPP_

#include <string>

struct NkSettings {
    std::wstring baseUrl;
    std::wstring token;
};

std::wstring NkSettingsPath();
std::wstring NkNormalizeUrl(std::wstring url);
bool NkLoadSettings(NkSettings& settings);
bool NkSaveSettings(const NkSettings& settings);
bool NkConfigured(const NkSettings& settings);

#include "nk_secure.hpp"

#endif
