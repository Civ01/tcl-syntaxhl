#include "TestUtil.h"

using namespace shl;
using namespace shl::test;

static const char* kCss = R"(/* header */
@import url(theme.css);
@media screen and (max-width: 600px) {
  body > .main #id:hover::before, a[href^="http"] {
    color: #ff0000 !important;
    margin: -1.5em 0 10% auto;
    background: url(img/a.png) no-repeat;
    font-family: "Segoe UI", sans-serif;
    width: calc(100% - 2px);
  }
}
@font-face {
  font-family: Foo;
  src: url("foo.woff2");
}
)";

TEST_CASE("css: selectors, properties, values") {
    auto l = lexLang("css", kCss);
    checkWellFormed(l);
    CHECK(l.at("/* header */") == "comment");
    CHECK(l.at("@import") == "at_rule");
    CHECK(l.at("@media") == "at_rule");
    CHECK(l.at("and") == "keyword");
    CHECK(l.at("max-width") == "property");
    CHECK(l.at("600px") == "number");
    CHECK(l.at("body") == "selector");
    CHECK(l.at(".main") == "selector");
    CHECK(l.at("#id") == "selector");
    CHECK(l.at(":hover") == "keyword");
    CHECK(l.at("::before") == "keyword");
    CHECK(l.at("href") == "key");
    CHECK(l.at("\"http\"") == "string");
    CHECK(l.at("color") == "property");
    CHECK(l.at("#ff0000") == "number");
    CHECK(l.at("!important") == "keyword");
    CHECK(l.at("-1.5em") == "number");
    CHECK(l.at("10%") == "number");
    CHECK(l.at("auto") == "value");
    CHECK(l.at("url", 2) == "function");
    CHECK(l.at("img/a.png") == "string");
    CHECK(l.at("no-repeat") == "value");
    CHECK(l.at("\"Segoe UI\"") == "string");
    CHECK(l.at("calc") == "function");
    CHECK(l.at("@font-face") == "at_rule");
    CHECK(l.at("font-family", 1) == "property");  // decl block of @font-face
    CHECK(l.at("Foo") == "value");
    CHECK(l.at("\"foo.woff2\"") == "string");
}

TEST_CASE("css: nesting without terminator on the line") {
    auto l = lexLang("css", "a {\n  color: red\n}\nbb,\ncc { top: 0 }\n.x { .y { left: 1px; } }");
    CHECK(l.at("color") == "property");
    CHECK(l.at("red") == "value");
    CHECK(l.at("bb") == "selector");
    CHECK(l.at("cc") == "selector");
    CHECK(l.at("top") == "property");
    CHECK(l.at(".y") == "selector");
    CHECK(l.at("left") == "property");
    CHECK(l.at("1px") == "number");
}

static const char* kScss = R"(// scss comment
$primary: #333;
@mixin flex($dir) { display: flex; flex-direction: $dir; }
.card {
  &:hover { color: $primary; }
  .title { font-size: 2em; }
  width: #{$w}px;
  @include flex(row);
}
@base: 10px;
.l { margin: @base; }
)";

TEST_CASE("css: scss/less mode") {
    auto l = lexLang("css", kScss, opts({{"support_scss_less", "true"}}));
    checkWellFormed(l);
    CHECK(l.at("// scss comment") == "comment");
    CHECK(l.at("$primary") == "variable");
    CHECK(l.at("#333") == "number");
    CHECK(l.at("@mixin") == "at_rule");
    CHECK(l.at("display") == "property");
    CHECK(l.at("$dir", 1) == "variable");
    CHECK(l.at("&") == "selector");
    CHECK(l.at(":hover") == "keyword");
    CHECK(l.at("$primary", 1) == "variable");
    CHECK(l.at(".title") == "selector");
    CHECK(l.at("font-size") == "property");
    CHECK(l.at("#{$w}") == "variable");
    CHECK(l.at("@include") == "at_rule");
    CHECK(l.at("@base") == "variable");
    CHECK(l.at("@base", 1) == "variable");

    auto plain = lexLang("css", kScss);
    CHECK(plain.at("// scss comment") != "comment");
}

TEST_CASE("css: resumable") {
    auto lx = createNativeLexer("css", opts());
    checkResumable(*lx, kCss);
    auto sx = createNativeLexer("css", opts({{"support_scss_less", "true"}}));
    checkResumable(*sx, kScss);
    checkResumable(*sx, "a { /* multi\nline */ color:\n red; }\n");
}
