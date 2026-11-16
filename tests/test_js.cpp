#include "TestUtil.h"

using namespace shl;
using namespace shl::test;

static const char* kJs = R"(#!/usr/bin/env node
/* block
   comment */
import { readFile } from 'fs';
const re = /ab+c\/[a/]/gi, half = total / 2 / count;
function* gen(a = 0b1010, b = 0xFFn) {
  yield `sum: ${a + b} and ${ { x: `inner ${b}` }.x } done`;
}
class Foo extends Bar {
  #secret = 1_000.5e10;
  get value() { return this.#secret ?? null; }
}
let s = "esc\t\u{1F600}" + 'line\
continued';
console.log(Math.max(1, .5), undefined);
const multi = `first
second ${x} third`;
ok = y || /re/.test(z);
)";

TEST_CASE("js: tokens") {
    auto l = lexLang("js", kJs);
    checkWellFormed(l);
    CHECK(l.at("#!/usr/bin/env node") == "comment");
    CHECK(l.at("/* block") == "comment");
    CHECK(l.at("comment */") == "comment");
    CHECK(l.at("import") == "keyword");
    CHECK(l.at("'fs'") == "string");
    CHECK(l.at("/ab+c\\/[a/]/gi") == "regex");
    CHECK(l.at("/ 2 /") == "<partial:operator>");
    CHECK(l.at("function") == "keyword");
    CHECK(l.at("gen") == "function");
    CHECK(l.at("0b1010") == "number");
    CHECK(l.at("0xFFn") == "number");
    CHECK(l.at("`sum: ${") == "template");
    CHECK(l.at("a + b") == "<partial:identifier>");
    CHECK(l.at("} and ${") == "template");
    CHECK(l.at("`inner ${") == "template");
    CHECK(l.at("} done`") == "template");
    CHECK(l.at("Foo") == "type");
    CHECK(l.at("Bar") == "identifier");
    CHECK(l.at("1_000.5e10") == "number");
    CHECK(l.at("this") == "keyword");
    CHECK(l.at("null") == "constant");
    CHECK(l.at("\\t") == "string_escape");
    CHECK(l.at("\\u{1F600}") == "string_escape");
    CHECK(l.at("continued'") == "string");
    CHECK(l.at("console") == "builtin");
    CHECK(l.at("max") == "function");
    CHECK(l.at(".5") == "number");
    CHECK(l.at("undefined") == "constant");
    CHECK(l.at("second ${") == "template");
    CHECK(l.at("} third`") == "template");
    CHECK(l.at("/re/") == "regex");
}

TEST_CASE("js: resumable") {
    auto lx = createNativeLexer("js", opts());
    checkResumable(*lx, kJs);
}
