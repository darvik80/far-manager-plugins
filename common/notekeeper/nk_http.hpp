#ifndef NK_HTTP_HPP_
#define NK_HTTP_HPP_

#include <string>

struct NkHttpResult {
    int status = 0;
    unsigned long winError = 0;
    std::string body;
};

NkHttpResult NkRequest(const std::wstring& method, const std::wstring& url, const std::wstring& token,
                       const std::string& body = {}, const std::wstring& extraHeader = {});

#endif
