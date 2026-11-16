#include "TestUtil.h"

using namespace shl;
using namespace shl::test;

static const char* kSample = R"({
  "name": "demo",
  "version": 1.5e3,
  "neg": -12,
  "ok": true, "none": null,
  "esc": "a\nbé",
  "list": [1, "two", {"k": false}],
  "nested": {"deep": {"x": [ ]}}
})";

TEST_CASE("json: keys vs values") {
    auto l = lexLang("json", kSample);
    checkWellFormed(l);
    CHECK(l.at("\"name\"") == "key");
    CHECK(l.at("\"demo\"") == "string");
    CHECK(l.at("1.5e3") == "number");
    CHECK(l.at("-12") == "number");
    CHECK(l.at("true") == "constant");
    CHECK(l.at("null") == "constant");
    CHECK(l.at("\\n") == "string_escape");
    CHECK(l.at("\"two\"") == "string");      // string inside array
    CHECK(l.at("\"k\"") == "key");           // key inside object inside array
    CHECK(l.at("\"deep\"") == "key");
    CHECK(l.at("{") == "punctuation");
}

TEST_CASE("json: unicode escape") {
    auto l = lexLang("json", "{\"e\": \"x\\u00e9y\"}");
    CHECK(l.at("\\u00e9") == "string_escape");
    CHECK(l.at("y\"") == "string");
}

TEST_CASE("json: errors and comments") {
    auto l = lexLang("json", "{\"a\": tru, \"b\": 01x} // c\n");
    CHECK(l.at("tru") == "error");
    CHECK(l.at("01x") == "error");
    CHECK(l.at("// c") == "error");

    auto c = lexLang("json", "{ // line\n /* block\n still */ \"a\": 1 }",
                     opts({{"support_jsonc_comments", "true"}}));
    CHECK(c.at("// line") == "comment");
    CHECK(c.at("/* block") == "comment");
    CHECK(c.at("still */") == "comment");
    CHECK(c.at("\"a\"") == "key");

    auto u = lexLang("json", "{\"open: 1\n\"b\": 2}");
    CHECK(u.at("\"open: 1") == "error");
}

TEST_CASE("json: resumable") {
    auto lx = createNativeLexer("json", opts({{"support_jsonc_comments", "true"}}));
    checkResumable(*lx, kSample);
    checkResumable(*lx, "[\n/* multi\nline */ {\"a\":\n\"b\"}\n]");
}
