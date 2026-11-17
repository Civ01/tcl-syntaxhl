#include <algorithm>
#include <filesystem>
#include <fstream>

#include "TestUtil.h"
#include "core/IniDocument.h"
#include "core/OptionSchema.h"
#include "core/Settings.h"
#include "core/Theme.h"

using namespace shl;
namespace fs = std::filesystem;

namespace {
fs::path writeTemp(const char* name, const std::string& content) {
    auto dir = fs::temp_directory_path() / "shl_settings_tests";
    fs::create_directories(dir);
    auto p = dir / name;
    std::ofstream(p, std::ios::binary) << content;
    return p;
}
std::string readAll(const fs::path& p) {
    std::string s;
    readFileBytes(p, s);
    return s;
}
}  // namespace

TEST_CASE("ini: remove key and section") {
    auto doc = IniDocument::parse(std::string_view("[A]\nx=1\ny=2\nx=3\n\n[B]\n; about b\nz=1\n\n[C]\nw=1\n"));
    CHECK(doc.remove("a", "X"));
    CHECK_FALSE(doc.get("A", "x"));
    CHECK(doc.get("A", "y").value() == "2");
    CHECK_FALSE(doc.remove("A", "nope"));
    CHECK(doc.removeSection("b"));
    CHECK_FALSE(doc.hasSection("B"));
    auto out = doc.serialize();
    CHECK(out.find("about b") == std::string::npos);
    CHECK(doc.get("C", "w").value() == "1");
}

TEST_CASE("schema: normalize and compare") {
    auto* size = findGeneralOption("max_highlight_size");
    REQUIRE(size);
    CHECK(size->normalize("2097152") == "2M");
    CHECK(size->sameValue("2M", "2048K"));
    CHECK(size->normalize("abc").empty());
    auto* tab = findGeneralOption("tab_width");
    CHECK(tab->normalize("8") == "8");
    CHECK(tab->normalize("0").empty());
    CHECK(tab->normalize("33").empty());
    CHECK(findGeneralOption("italic_comments")->sameValue("yes", "true"));
    CHECK(findGeneralOption("tokenizer")->normalize("REGEX") == "regex");
    CHECK(findGeneralOption("tokenizer")->normalize("other").empty());
    CHECK(findGeneralOption("theme")->sameValue("Auto", "auto"));
    // Every general option round-trips through Config with its default.
    Config c = Config::defaults();
    for (const auto& d : generalOptions()) {
        CAPTURE(d.key);
        CHECK(d.sameValue(getGeneralValue(c, d.key), d.defaultValue));
        CHECK(setGeneralValue(c, d.key, d.defaultValue));
    }
    CHECK_FALSE(setGeneralValue(c, "font_size", "200"));
    CHECK_FALSE(setGeneralValue(c, "nope", "1"));
    CHECK(lexerOptions("json").size() == 1);
    CHECK(lexerOptions("css").size() == 1);
    CHECK(lexerOptions("python").empty());
}

TEST_CASE("settings: unchanged config saves the file byte-identical") {
    std::string shipped = readAll(fs::path(SHL_SOURCE_DIR) / "dist" / "tcl-syntaxhl.ini");
    // serialize() normalises line endings to CRLF; compare after a first save.
    auto p = writeTemp("same.ini", shipped);
    Settings s;
    REQUIRE(s.load(p));
    REQUIRE(s.save());
    std::string first = readAll(p);
    Settings s2;
    REQUIRE(s2.load(p));
    REQUIRE(s2.save());
    CHECK(readAll(p) == first);
    // Same content apart from line endings.
    std::string a = first, b = shipped;
    a.erase(std::remove(a.begin(), a.end(), '\r'), a.end());
    b.erase(std::remove(b.begin(), b.end(), '\r'), b.end());
    if (!b.empty() && b.back() != '\n') b += '\n';
    CHECK(a == b);
}

TEST_CASE("settings: equivalent spellings are not rewritten") {
    auto p = writeTemp("spell.ini", "[General]\nmax_highlight_size=2097152\nitalic_comments=yes\n");
    Settings s;
    REQUIRE(s.load(p));
    REQUIRE(s.save());
    auto out = readAll(p);
    CHECK(out.find("max_highlight_size=2097152") != std::string::npos);
    CHECK(out.find("italic_comments=yes") != std::string::npos);
    CHECK(out.find("tab_width") == std::string::npos);  // defaults are not added
}

TEST_CASE("settings: general changes, keeping comments and unknown keys") {
    auto p = writeTemp("gen.ini", "; header\n[General]\n; size comment\nmax_highlight_size=2M\ncustom=1\n");
    Settings s;
    REQUIRE(s.load(p));
    s.config().maxHighlightSize = 8u * 1024 * 1024;
    s.config().tabWidth = 8;
    s.config().italicComments = false;
    REQUIRE(s.save());
    auto out = readAll(p);
    CHECK(out.find("; header") != std::string::npos);
    CHECK(out.find("; size comment\r\nmax_highlight_size=8M") != std::string::npos);
    CHECK(out.find("custom=1") != std::string::npos);
    CHECK(out.find("tab_width=8") != std::string::npos);
    CHECK(out.find("italic_comments=false") != std::string::npos);
    Settings back;
    REQUIRE(back.load(p));
    CHECK(back.config().tabWidth == 8);
    CHECK(back.config().maxHighlightSize == 8u * 1024 * 1024);
}

TEST_CASE("settings: languages added, changed, removed; options set and cleared") {
    auto p = writeTemp("lang.ini",
                       "[Languages]\nJSON=json\nCSS=css\nINI=ini\n\n[JSON]\nsupport_jsonc_comments=false\n\n"
                       "[CSS]\n; css options\nsupport_scss_less=true\n");
    Settings s;
    REQUIRE(s.load(p));
    auto& langs = s.config().languages;
    REQUIRE(langs.size() == 3);
    // change JSON: extensions + option + override
    langs[0].extensions = {"json", "jsonc"};
    langs[0].options.values["support_jsonc_comments"] = "true";
    langs[0].options.values["tab_width"] = "2";
    // remove CSS completely
    langs.erase(langs.begin() + 1);
    // clear an override that does not exist (no-op) and add a new language
    auto pac = makeLanguage("PAC", "pac");
    pac.options.values["lexer"] = "js";
    refreshLanguage(pac);
    langs.push_back(pac);
    REQUIRE(s.save());
    auto out = readAll(p);
    CHECK(out.find("CSS=") == std::string::npos);
    CHECK(out.find("[CSS]") == std::string::npos);
    CHECK(out.find("; css options") == std::string::npos);
    CHECK(out.find("JSON=json,jsonc") != std::string::npos);
    CHECK(out.find("support_jsonc_comments=true") != std::string::npos);
    CHECK(out.find("PAC=pac") != std::string::npos);
    CHECK(out.find("[PAC]\r\nlexer=js") != std::string::npos);

    Settings back;
    REQUIRE(back.load(p));
    auto& cfg = back.config();
    REQUIRE(cfg.languageForExtension("pac"));
    CHECK(cfg.languageForExtension("pac")->lexerId == "js");
    CHECK(cfg.tabWidthFor(*cfg.languageForExtension("jsonc")) == 2);
    CHECK_FALSE(cfg.languageForExtension("css"));

    // Clearing an override removes the key.
    back.config().languages[0].options.values.erase("tab_width");
    REQUIRE(back.save());
    CHECK(readAll(p).find("tab_width") == std::string::npos);
}

TEST_CASE("settings: language names and theme sets") {
    CHECK(isValidLanguageName("C++"));
    CHECK(isValidLanguageName("my_lang-2"));
    CHECK_FALSE(isValidLanguageName(""));
    CHECK_FALSE(isValidLanguageName("a b"));
    CHECK_FALSE(isValidLanguageName("x/y"));
    CHECK_FALSE(isValidLanguageName("general"));
    CHECK_FALSE(isValidLanguageName("Languages"));
    auto sets = listThemeSets(fs::path(SHL_SOURCE_DIR) / "dist");
    CHECK(sets == std::vector<std::string>{"dark", "light"});
    CHECK(listThemeSets("/nonexistent/dir").empty());
}
