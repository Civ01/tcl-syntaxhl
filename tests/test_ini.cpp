#include <filesystem>

#include "TestUtil.h"
#include "core/IniDocument.h"
#include "regex/RegexLexer.h"

using namespace shl;
using namespace shl::test;

static const char* kIni = R"(; comment
# hash comment
[Section One]
key = value with spaces
path="C:\dir\file.txt"
count=42
ratio=-0.5
flag=true
mixed=abc "quoted" def
name: colon style
  [Indented] ; trailing
orphan line
)";

TEST_CASE("ini: tokens") {
    auto l = lexLang("ini", kIni);
    checkWellFormed(l);
    CHECK(l.at("; comment") == "comment");
    CHECK(l.at("# hash comment") == "comment");
    CHECK(l.at("[") == "section_bracket");
    CHECK(l.at("Section One") == "section");
    CHECK(l.at("]") == "section_bracket");
    CHECK(l.at("key") == "key");
    CHECK(l.at("=") == "operator");
    CHECK(l.at("value with spaces") == "value");
    CHECK(l.at("\"C:\\dir\\file.txt\"") == "string");
    CHECK(l.at("42") == "number");
    CHECK(l.at("-0.5") == "number");
    CHECK(l.at("true") == "constant");
    CHECK(l.at("\"quoted\"") == "string");
    CHECK(l.at("abc ") == "value");
    CHECK(l.at("name") == "key");
    CHECK(l.at("colon style") == "value");
    CHECK(l.at("Indented") == "section");
    CHECK(l.at("[", 1) == "section_bracket");
    CHECK(l.at("; trailing") == "comment");
    CHECK(l.at("orphan line") == "value");
}

TEST_CASE("ini: unterminated section ends at end of line") {
    auto l = lexLang("ini", "[Broken\nkey=1\n");
    CHECK(l.at("[") == "section_bracket");
    CHECK(l.at("Broken") == "section");
    CHECK(l.at("key") == "key");
    CHECK(l.at("1") == "number");
}

TEST_CASE("ini: regex rules match the native lexer") {
    auto native = createNativeLexer("ini", opts());
    IniDocument doc;
    REQUIRE(IniDocument::loadFile(std::filesystem::path(SHL_SOURCE_DIR) / "dist" / "rules" / "ini.rules", doc));
    auto rules = parseRules(doc);
    INFO(rules.error);
    REQUIRE(rules.lexer);
    const char* extra = "[Broken\nkey=1\nk2 = 'single'\n  spaced key  =  +3.5e2  \nempty=\nflag = Off\n";
    for (const std::string& src : {std::string(kIni), std::string(extra)}) {
        auto a = lexWith(*native, src);
        auto b = lexWith(*rules.lexer, src);
        // Compare the class of every non-blank character (token boundaries may
        // differ, e.g. regex tokens include leading blanks).
        auto classAt = [](const Lexed& l, size_t pos) {
            for (const auto& t : l.tokens)
                if (t.start <= pos && pos < t.start + t.length) return t.cls;
            return TokenClass::Default;
        };
        std::string report;
        int bad = 0;
        for (size_t i = 0; i < a.text.size(); ++i) {
            if (isSpace(a.text[i]) || isNewline(a.text[i])) continue;
            std::string na = tokenClassName(classAt(a, i)), nb = tokenClassName(classAt(b, i));
            if (na != nb && ++bad <= 8)
                report += "  @" + std::to_string(i) + " '" + utf16ToUtf8(a.text.substr(i, 10)) + "': native=" + na +
                          " regex=" + nb + "\n";
        }
        INFO(report);
        CHECK(bad == 0);
        checkResumable(*rules.lexer, src);
    }
}

TEST_CASE("ini: resumable") {
    auto lx = createNativeLexer("ini", opts());
    checkResumable(*lx, kIni);
}
