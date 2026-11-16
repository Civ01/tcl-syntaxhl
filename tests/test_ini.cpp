#include "TestUtil.h"

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
    CHECK(l.at("[Section One]") == "section");
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
    CHECK(l.at("[Indented]") == "section");
    CHECK(l.at("; trailing") == "comment");
    CHECK(l.at("orphan line") == "value");
}

TEST_CASE("ini: resumable") {
    auto lx = createNativeLexer("ini", opts());
    checkResumable(*lx, kIni);
}
