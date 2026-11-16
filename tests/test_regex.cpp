#include <filesystem>

#include "TestUtil.h"
#include "core/IniDocument.h"
#include "regex/RegexLexer.h"

using namespace shl;
using namespace shl::test;
namespace fs = std::filesystem;

static LexerPtr shippedJsonRules() {
    IniDocument doc;
    REQUIRE(IniDocument::loadFile(fs::path(SHL_SOURCE_DIR) / "dist" / "rules" / "json.rules", doc));
    auto r = parseRules(doc);
    INFO(r.error);
    REQUIRE(r.lexer);
    return std::move(r.lexer);
}

TEST_CASE("regex: shipped json.rules") {
    auto lx = shippedJsonRules();
    auto l = lexWith(*lx, "{\n  \"name\": \"demo\\n\", \"n\": -1.5e3,\n  \"ok\": [true, null] /* c\n */ }\n");
    checkWellFormed(l);
    CHECK(l.at("\"name\"") == "key");
    CHECK(l.at("\"demo") == "string");
    CHECK(l.at("\\n") == "string_escape");
    CHECK(l.at("-1.5e3") == "number");
    CHECK(l.at("true") == "constant");
    CHECK(l.at("null") == "constant");
    CHECK(l.at("[") == "punctuation");
    CHECK(l.at("/* c") == "comment");
    CHECK(l.at(" */") == "comment");
    checkResumable(*lx, "{ /* a\nb\nc */ \"k\": 1 }\n[\"x\"]");
}

TEST_CASE("regex: rule parsing errors") {
    auto bad = [](const char* src) {
        auto r = parseRules(IniDocument::parse(std::string_view(src)));
        CHECK_FALSE(r.lexer);
        CHECK_FALSE(r.error.empty());
        return r.error;
    };
    bad("[Rules]\n1=nosuchclass:x\n");
    bad("[Rules]\n1=string:(unclosed\n");
    bad("[Rules]\n1=string\n");
    bad("[Rules]\nx=string:a\n");
    bad("[Rules]\n1=string:a\n1.push=missing\n");
    bad("[Rules]\n1.pop=true\n");
    bad("[Meta]\nname=x\n");
}

TEST_CASE("regex: states, ordering, zero-length guard") {
    auto r = parseRules(IniDocument::parse(std::string_view(
        "[Rules]\n"
        "20=keyword:\\bif\\b\n"
        "10=string:<\n"
        "10.push=tag\n"
        "30=number:(?=x)\n"  // zero-length, no state change: ignored
        "[Rules.tag]\n"
        "default=string\n"
        "5=string:>\n"
        "5.pop=true\n")));
    INFO(r.error);
    REQUIRE(r.lexer);
    auto l = lexWith(*r.lexer, "if <a\nif b> if x");
    CHECK(l.at("if") == "keyword");
    CHECK(l.at("<a") == "string");
    CHECK(l.at("if b>") == "string");  // still inside the tag state
    CHECK(l.at("if", 2) == "keyword");
    CHECK(l.at("x") == "default");
}
