#include <algorithm>

#include "TestUtil.h"
#include "core/LazyModel.h"

using namespace shl;
using namespace shl::test;

static std::u16string bigPython(int blocks) {
    std::string s;
    for (int i = 0; i < blocks; ++i) {
        s += "def f" + std::to_string(i) + "(x):\n";
        s += "    \"\"\"doc\n    string " + std::to_string(i) + "\n    \"\"\"\n";
        s += "    return x + " + std::to_string(i) + "  # c\n";
    }
    return utf8ToUtf16(s);
}

TEST_CASE("lazy: tokens equal full lexing, in any request order") {
    auto text = bigPython(360);  // 1800 lines
    auto lx = createNativeLexer("python", opts());
    std::vector<Token> full;
    LexState st;
    lx->lex(text, st, full);

    LazyModel model(text, *lx, 64);
    CHECK(model.lineCount() == 1800);
    std::vector<Token> lazy;
    auto take = [&](size_t a, size_t b) {
        auto t = model.takeTokens(a, b);
        lazy.insert(lazy.end(), t.begin(), t.end());
    };
    take(1500, 1560);  // jump far ahead first
    take(10, 20);
    take(0, 15);       // overlaps already-done lines
    take(1550, 1799);
    take(0, 1799);     // everything else
    CHECK(model.takeTokens(0, 1799).empty());
    std::sort(lazy.begin(), lazy.end(), [](const Token& a, const Token& b) { return a.start < b.start; });
    lazy.erase(std::unique(lazy.begin(), lazy.end()), lazy.end());
    REQUIRE(lazy.size() == full.size());
    for (size_t i = 0; i < full.size(); ++i) {
        CAPTURE(i);
        CHECK(lazy[i] == full[i]);
    }
}

TEST_CASE("lazy: line helpers") {
    std::u16string t = u"a\rbb\r\rccc";
    auto lx = createNativeLexer("ini", opts());
    LazyModel m(t, *lx);
    CHECK(m.lineCount() == 4);
    CHECK(m.lineStart(1) == 2);
    CHECK(m.lineStart(3) == 6);
    CHECK(m.lineStart(9) == t.size());
    CHECK(m.lineOfOffset(0) == 0);
    CHECK(m.lineOfOffset(3) == 1);
    CHECK(m.lineOfOffset(7) == 3);
}

TEST_CASE("robustness: random input never breaks lexers") {
    const char* ids[] = {"json", "css", "python", "js", "ini"};
    const char16_t alphabet[] = u"{}[]()<>\"'`$#@&*/\\:;,.=+-!?%^|~ \t\r\nabcxyz019_eEfFrRbBuUé中\xD800";
    uint32_t seed = 12345;
    auto rnd = [&] { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7FFF; };
    for (auto id : ids) {
        for (bool opt : {false, true}) {
            auto lx = createNativeLexer(id, opts({{"support_jsonc_comments", opt ? "true" : "false"},
                                                 {"support_scss_less", opt ? "true" : "false"}}));
            for (int iter = 0; iter < 200; ++iter) {
                Lexed l;
                size_t len = rnd() % 400;
                for (size_t i = 0; i < len; ++i) l.text.push_back(alphabet[rnd() % (sizeof(alphabet) / 2 - 1)]);
                LexState st;
                lx->lex(l.text, st, l.tokens);
                checkWellFormed(l);
            }
        }
    }
}
