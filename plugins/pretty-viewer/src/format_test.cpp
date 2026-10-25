#include "format.hpp"

#include <cstdio>
#include <string>

static int g_failed = 0;

static std::wstring Dump(const ViewDoc& doc) {
    std::wstring text;
    for (size_t i = 0; i < doc.lines.size(); ++i) {
        if (i) text += L"\n";
        if (!doc.lines[i].spans.empty() && doc.lines[i].spans[0].token == Token::Error) continue;
        text += JoinLine(doc.lines[i]);
    }
    return text;
}

static void Expect(const char* name, const std::wstring& ext, const std::wstring& input, const std::wstring& want) {
    const ViewDoc doc = FormatDocument(input, ext);
    const std::wstring got = Dump(doc);
    if (got == want && doc.error.empty()) {
        std::printf("ok  %s\n", name);
        return;
    }
    std::printf("FAIL %s\n", name);
    if (!doc.error.empty()) std::printf("  error present\n");
    std::printf("  ---\n");
    for (wchar_t ch : got) {
        if (ch == L'\n') std::printf("\\n\n");
        else std::printf("%lc", ch);
    }
    std::printf("\n  ---\n");
    ++g_failed;
}

int main() {
    Expect("json", L".json",
           L"{\"a\":1,\"b\":[true,null,\"x\"]}",
           L"{\n"
           L"  \"a\": 1,\n"
           L"  \"b\": [\n"
           L"    true,\n"
           L"    null,\n"
           L"    \"x\"\n"
           L"  ]\n"
           L"}");
    Expect("json-empty", L".json", L"{}", L"{}");
    Expect("yaml", L".yaml",
           L"a: 1\nb:\n  - x\n  - y\n",
           L"a: 1\n"
           L"b:\n"
           L"  - x\n"
           L"  - y");
    Expect("xml", L".xml",
           L"<root><a b=\"1\">t</a></root>",
           L"<root>\n"
           L"  <a b=\"1\">t</a>\n"
           L"</root>");
    Expect("xml-decl", L".xml",
           L"<?xml version=\"1.0\"?><root/>",
           L"<?xml version=\"1.0\"?>\n"
           L"<root/>");
    if (g_failed) {
        std::printf("%d failed\n", g_failed);
        return 1;
    }
    std::printf("all ok\n");
    return 0;
}
