#ifndef NK_JSON_HPP_
#define NK_JSON_HPP_

#include <map>
#include <string>
#include <vector>

struct NkObject {
    std::map<std::string, std::wstring> str;
    std::map<std::string, bool> boolean;
    std::map<std::string, int> number;
    std::map<std::string, std::vector<std::wstring>> arrays;
    std::map<std::string, NkObject> objects;
    std::map<std::string, std::vector<NkObject>> objectArrays;

    std::wstring S(const char* key) const {
        auto it = str.find(key);
        return it == str.end() ? std::wstring{} : it->second;
    }
    bool B(const char* key, bool fallback = false) const {
        auto it = boolean.find(key);
        return it == boolean.end() ? fallback : it->second;
    }
    int N(const char* key, int fallback = 0) const {
        auto it = number.find(key);
        return it == number.end() ? fallback : it->second;
    }
    std::vector<std::wstring> A(const char* key) const {
        auto it = arrays.find(key);
        return it == arrays.end() ? std::vector<std::wstring>{} : it->second;
    }
};

bool NkParseObject(const std::string& text, size_t& i, NkObject& out, bool nested = false);
bool NkParseObjectArray(const std::string& json, std::vector<NkObject>& out);
bool NkMcpPayload(const std::string& json, std::string& payload, std::wstring& error);
std::string NkJsonEscape(const std::wstring& wide);
std::string NkJsonStringArray(const std::vector<std::wstring>& values);

#endif
