#ifndef NK_SECURE_HPP_
#define NK_SECURE_HPP_

#include <string>

bool NkUrlIsSecure(const std::wstring& url);
std::wstring NkProtectSecret(const std::wstring& plain);
std::wstring NkUnprotectSecret(const std::wstring& stored);
bool NkRestrictToCurrentUser(const std::wstring& path);
std::wstring NkSecureTempDir(const wchar_t* leaf);
std::wstring NkSecureTempPath(const wchar_t* leaf, const wchar_t* prefix, const wchar_t* ext,
                              std::wstring& error);
bool NkWriteSecureFile(const std::wstring& path, const void* data, size_t size);
void NkWipeTempPrefix(const wchar_t* leaf, const wchar_t* prefix);

#endif
