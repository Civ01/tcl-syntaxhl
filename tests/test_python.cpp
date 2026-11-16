#include "TestUtil.h"

using namespace shl;
using namespace shl::test;

static const char* kPy = R"(#!/usr/bin/env python3
"""Module docstring
spanning lines."""
import os
from typing import List

@dataclass(frozen=True)
class Point(Base):
    x: int = 0x1F
    def norm(self, k=1_000.5e-3j) -> float:
        s = f"x={self.x}\n" + r'\d+' + b'\x00'
        return abs(s) if self.x is not None else False  # trailing

match command:
    case [x, *rest]:
        pass
type Alias = List[int]
match = 3
print(len('''one
two'''), "cont\
inued")
)";

TEST_CASE("python: tokens") {
    auto l = lexLang("python", kPy);
    checkWellFormed(l);
    CHECK(l.at("#!/usr/bin/env python3") == "comment");
    CHECK(l.at("\"\"\"Module docstring") == "string");
    CHECK(l.at("spanning lines.\"\"\"") == "string");
    CHECK(l.at("import") == "keyword");
    CHECK(l.at("@dataclass") == "decorator");
    CHECK(l.at("True") == "constant");
    CHECK(l.at("class", 1) == "keyword");
    CHECK(l.at("Point") == "type");
    CHECK(l.at("0x1F") == "number");
    CHECK(l.at("def") == "keyword");
    CHECK(l.at("norm") == "function");
    CHECK(l.at("self") == "builtin");
    CHECK(l.at("1_000.5e-3j") == "number");
    CHECK(l.at("f\"x=") == "string");
    CHECK(l.at("\\n") == "string_escape");
    CHECK(l.at("r'\\d+'") == "string");  // raw: no escape token
    CHECK(l.at("\\x00") == "string_escape");
    CHECK(l.at("return") == "keyword");
    CHECK(l.at("abs") == "builtin");
    CHECK(l.at("None") == "constant");
    CHECK(l.at("# trailing") == "comment");
    CHECK(l.at("match") == "keyword");
    CHECK(l.at("case") == "keyword");
    CHECK(l.at("type") == "keyword");
    CHECK(l.at("match", 1) == "identifier");  // match = 3
    CHECK(l.at("two'''") == "string");
    CHECK(l.at("inued\"") == "string");
    CHECK(l.at("->") == "operator");
}

TEST_CASE("python: resumable") {
    auto lx = createNativeLexer("python", opts());
    checkResumable(*lx, kPy);
}
