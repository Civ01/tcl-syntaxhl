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

static LexerPtr shippedRules(const char* name) {
    IniDocument doc;
    REQUIRE(IniDocument::loadFile(fs::path(SHL_SOURCE_DIR) / "dist" / "rules" / name, doc));
    auto r = parseRules(doc);
    INFO(name << ": " << r.error);
    REQUIRE(r.lexer);
    return std::move(r.lexer);
}

static std::string sampleText(const char* name) {
    std::string s;
    REQUIRE(readFileBytes(fs::path(SHL_SOURCE_DIR) / "tests" / "samples" / name, s));
    return s;
}

TEST_CASE("regex: js.rules") {
    auto lx = shippedRules("js.rules");
    auto l = lexWith(*lx, R"(#!/usr/bin/env node
/* block
   comment */ import { x } from 'mod';
const re = /ab+c\/[a/]/gi, half = total / 2 / count;
function* gen(a = 0b1010, b = 0xFFn) {
  return `sum ${a + `in ${b}`} "q" done` + "esc\t";
}
class Foo extends Bar { #p = 1_000.5e3; }
console.log(Math.max(1, .5), undefined, null);
x = (
  /^start/.test(s));
)");
    checkWellFormed(l);
    CHECK(l.at("#!/usr/bin/env node") == "comment");
    CHECK(l.at("comment */") == "comment");
    CHECK(l.at("import") == "keyword");
    CHECK(l.at("'mod'") == "string");
    CHECK(l.at("const") == "keyword");
    CHECK(l.at("/ab+c\\/[a/]/gi") == "regex");
    CHECK(l.at("/ 2 /") == "<partial:operator>");
    CHECK(l.at("function*") == "keyword");
    CHECK(l.at("gen") == "function");
    CHECK(l.at("0b1010") == "number");
    CHECK(l.at("0xFFn") == "number");
    CHECK(l.at("return") == "keyword");
    CHECK(l.at("`sum ${") == "template");
    CHECK(l.at("`in ${") == "template");
    CHECK(l.at("} \"q\" done`") == "template");
    CHECK(l.at("\\t") == "string_escape");
    CHECK(l.at("Foo") == "function");  // class name (regex backend: same class as function names)
    CHECK(l.at("1_000.5e3") == "number");
    CHECK(l.at("console") == "builtin");
    CHECK(l.at("max") == "function");
    CHECK(l.at(".5") == "number");
    CHECK(l.at("undefined") == "constant");
    CHECK(l.at("/^start/") == "regex");
    checkResumable(*lx, sampleText("sample.js"));
}

TEST_CASE("regex: css.rules") {
    auto lx = shippedRules("css.rules");
    auto text = sampleText("sample.css");
    auto l = lexWith(*lx, text);
    checkWellFormed(l);
    CHECK(l.at("/* Sample stylesheet */") == "comment");
    CHECK(l.at("@import") == "at_rule");
    CHECK(l.at("\"base.css\"") == "string");
    CHECK(l.at(":root") == "keyword");
    CHECK(l.at("--accent") == "property");
    CHECK(l.at("#0a84ff") == "number");
    CHECK(l.at("body") == "selector");
    CHECK(l.at(".card") == "selector");
    CHECK(l.at(":hover") == "keyword");
    CHECK(l.at("href") == "key");
    CHECK(l.at("\"https\"") == "string");
    CHECK(l.at("color") == "property");
    CHECK(l.at("var") == "function");
    CHECK(l.at("!important") == "keyword");
    CHECK(l.at("-1.5em") == "number");
    CHECK(l.at("10%") == "number");
    CHECK(l.at("auto") == "value");
    CHECK(l.at("img/bg.png") == "string");
    CHECK(l.at("no-repeat") == "value");
    CHECK(l.at("@media") == "at_rule");
    CHECK(l.at("and") == "keyword");
    CHECK(l.at("max-width") == "property");
    CHECK(l.at("600px") == "number");
    CHECK(l.at(".card", 1) == "selector");  // nested rule inside @media
    CHECK(l.at("width") == "property");
    CHECK(l.at("calc") == "function");
    checkResumable(*lx, text);
}

TEST_CASE("regex: shipped rules survive random input and are resumable on samples") {
    const char* files[] = {"json.rules", "ini.rules", "css.rules", "js.rules"};
    const char* samples[] = {"sample.json", "sample.ini", "sample.css", "sample.js"};
    const char16_t alphabet[] = u"{}[]()<>\"'`$#@&*/\\:;,.=+-!?%^|~ \t\r\nabcxyz019_eEé";
    uint32_t seed = 777;
    auto rnd = [&] { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7FFF; };
    for (int f = 0; f < 4; ++f) {
        CAPTURE(files[f]);
        auto lx = shippedRules(files[f]);
        checkResumable(*lx, sampleText(samples[f]));
        for (int iter = 0; iter < 60; ++iter) {
            Lexed l;
            size_t len = rnd() % 300;
            for (size_t i = 0; i < len; ++i) l.text.push_back(alphabet[rnd() % (sizeof(alphabet) / 2 - 1)]);
            LexState st;
            lx->lex(l.text, st, l.tokens);
            checkWellFormed(l);
        }
    }
}

TEST_CASE("themes: language theme files list every class their lexers emit") {
    struct Case { const char* lang; const char* rules; const char* sample; } cases[] = {
        {"ini", "ini.rules", "sample.ini"}, {"js", "js.rules", "sample.js"}, {"css", "css.rules", "sample.css"}};
    for (auto& c : cases) {
        CAPTURE(c.lang);
        std::vector<LexerPtr> lexers;
        lexers.push_back(createNativeLexer(c.lang, opts({{"support_scss_less", "true"}})));
        lexers.push_back(shippedRules(c.rules));
        std::string text = sampleText(c.sample);
        for (const char* set : {"light", "dark"}) {
            IniDocument doc;
            REQUIRE(IniDocument::loadFile(fs::path(SHL_SOURCE_DIR) / "dist" / "themes" / set /
                                              (std::string(c.lang) + ".theme"),
                                          doc));
            for (auto& lx : lexers) {
                auto l = lexWith(*lx, text);
                for (const auto& t : l.tokens) {
                    CAPTURE(set);
                    CAPTURE(tokenClassName(t.cls));
                    CHECK(doc.get("Tokens", tokenClassName(t.cls)).has_value());
                }
            }
        }
    }
}
