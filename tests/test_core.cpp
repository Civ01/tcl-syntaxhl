#include <filesystem>
#include <fstream>

#include "TestUtil.h"
#include "core/Config.h"
#include "core/IniDocument.h"
#include "core/LexerFactory.h"
#include "core/Settings.h"
#include "core/TextDecoder.h"
#include "core/Theme.h"

using namespace shl;
namespace fs = std::filesystem;

static const fs::path kDist = fs::path(SHL_SOURCE_DIR) / "dist";

TEST_CASE("ini: sections, keys, comments, order") {
    auto doc = IniDocument::parse(std::string_view(
        "; top comment\r\n"
        "[General]\r\n"
        "Tokenizer = regex\r\n"
        "# hash comment\n"
        "color=#FF0000 ; not a comment\n"
        "\n"
        "[rules]\n"
        "2=b\n"
        "1=a\n"));
    CHECK(doc.get("general", "TOKENIZER").value() == "regex");
    CHECK(doc.get("General", "color").value() == "#FF0000 ; not a comment");
    auto e = doc.entries("Rules");
    REQUIRE(e.size() == 2);
    CHECK(e[0].first == "2");
    CHECK(e[1].second == "a");
    CHECK_FALSE(doc.get("General", "missing").has_value());
    CHECK(doc.sections() == std::vector<std::string>{"General", "rules"});
}

TEST_CASE("ini: set preserves comments and appends") {
    auto doc = IniDocument::parse(std::string_view("; keep me\n[A]\nx=1\n; tail\n"));
    doc.set("A", "x", "2");
    doc.set("A", "y", "3");
    doc.set("B", "z", "4");
    auto out = doc.serialize();
    CHECK(out.find("; keep me") != std::string::npos);
    CHECK(out.find("; tail") != std::string::npos);
    CHECK(out.find("x=2") != std::string::npos);
    auto re = IniDocument::parse(std::string_view(out));
    CHECK(re.get("A", "y").value() == "3");
    CHECK(re.get("B", "z").value() == "4");
    CHECK(out.find("x=2\r\ny=3") != std::string::npos);
}

TEST_CASE("config: defaults and parsing") {
    auto c = Config::defaults();
    CHECK(c.tokenizer == TokenizerKind::Native);
    CHECK(c.maxHighlightSize == 2u * 1024 * 1024);
    CHECK(c.italicComments);
    CHECK_FALSE(c.useLazyHighlight);
    REQUIRE(c.languageForExtension("PYW"));
    CHECK(c.languageForExtension("pyw")->id == "python");
    CHECK(c.languageForFile("C:/x/y.JSON")->id == "json");
    CHECK(c.languageForExtension("txt") == nullptr);

    auto doc = IniDocument::parse(std::string_view(
        "[General]\nmax_highlight_size=512K\nuse_lazy_highlight=yes\ntheme=Mono\nfont_size=12\n"
        "[Languages]\nJSON=json,jsonc\nJSON5=json5\n"
        "[JSON]\nsupport_jsonc_comments=true\ntokenizer=regex\n[JSON5]\nlexer=json\n"));
    auto cfg = Config::fromIni(doc);
    CHECK(cfg.maxHighlightSize == 512u * 1024);
    CHECK(cfg.useLazyHighlight);
    CHECK(cfg.fontSize == 12);
    CHECK(cfg.themeSet(true) == "Mono");
    REQUIRE(cfg.languages.size() == 2);
    auto* j = cfg.languageForExtension("jsonc");
    REQUIRE(j);
    CHECK(j->options.getBool("support_jsonc_comments", false));
    CHECK(cfg.tokenizerFor(*j) == TokenizerKind::Regex);
    auto* j5 = cfg.languageForExtension("json5");
    REQUIRE(j5);
    CHECK(j5->lexerId == "json");
    CHECK(j5->id == "json5");
    CHECK(cfg.tokenizerFor(*j5) == TokenizerKind::Native);
}

TEST_CASE("config: italic_comments global and per language") {
    auto cfg = Config::fromIni(IniDocument::parse(std::string_view(
        "[General]\nitalic_comments=false\n[INI]\nitalic_comments=true\n")));
    CHECK_FALSE(cfg.italicComments);
    CHECK(cfg.italicCommentsFor(*cfg.languageForExtension("ini")));
    CHECK_FALSE(cfg.italicCommentsFor(*cfg.languageForExtension("json")));
    auto def = Config::fromIni(IniDocument::parse(std::string_view("[JSON]\nitalic_comments=no\n")));
    CHECK(def.italicCommentsFor(*def.languageForExtension("py")));
    CHECK_FALSE(def.italicCommentsFor(*def.languageForExtension("json")));
    Theme t = Theme::builtin(false);
    CHECK((t.style(TokenClass::Comment).flags & kItalic) != 0);
    t.disableCommentItalic();
    CHECK((t.style(TokenClass::Comment).flags & kItalic) == 0);
}

TEST_CASE("theme: section_bracket inherits punctuation, @alias, flags") {
    auto base = [] {
        Theme t = Theme::builtin(false);
        t.apply(IniDocument::parse(std::string_view("[Tokens]\npunctuation=#111111\nsection=#800000,,bold\n")));
        return t;
    };
    {
        Theme t = base();
        t.finalize();
        CHECK(t.style(TokenClass::SectionBracket).fg == 0x111111);  // follows punctuation
    }
    {
        Theme t = base();
        t.apply(IniDocument::parse(std::string_view("[Tokens]\nsection_bracket=@section\n")));
        // A later file changes section: the alias follows.
        t.apply(IniDocument::parse(std::string_view("[Tokens]\nsection=#00AA00,,normal\n")));
        t.finalize();
        CHECK(t.style(TokenClass::SectionBracket).fg == 0x00AA00);
        CHECK(t.style(TokenClass::SectionBracket).flags == 0);
        CHECK(t.style(TokenClass::Section).flags == 0);
    }
    {
        Theme t = base();
        t.apply(IniDocument::parse(std::string_view("[Tokens]\nsection_bracket=#808080\nsection=#123456\n")));
        t.finalize();
        CHECK(t.style(TokenClass::SectionBracket).fg == 0x808080);
        CHECK(t.style(TokenClass::Section).flags == kBold);  // flags omitted: inherited
    }
    {
        Theme t = base();
        std::vector<std::string> warn;
        t.apply(IniDocument::parse(std::string_view("[Tokens]\nkey=@nope\nvalue=@value\nstring=@key\nkey=@string\n")),
                &warn);
        t.finalize();  // cycle string<->key must not hang
        CHECK(warn.size() == 2);
    }
}

TEST_CASE("config: theme auto selection") {
    Config c;
    CHECK(c.themeSet(false) == "light");
    CHECK(c.themeSet(true) == "dark");
}

TEST_CASE("config: sizes and bools") {
    CHECK(parseSize("5M", 0) == 5u * 1024 * 1024);
    CHECK(parseSize("100", 0) == 100);
    CHECK(parseSize(" 2 kb", 0) == 2048);
    CHECK(parseSize("1G", 0) == 1024ull * 1024 * 1024);
    CHECK(parseSize("abc", 7) == 7);
    CHECK(parseSize("5X", 7) == 7);
    CHECK(parseBool("TRUE", false));
    CHECK_FALSE(parseBool("off", true));
    CHECK(parseBool("maybe", true));
    CHECK(formatSize(5u * 1024 * 1024) == "5 MB");
    CHECK(formatSize(13002342) == "12.4 MB");
}

TEST_CASE("config: detect string") {
    auto c = Config::defaults();
    auto s = c.buildDetectString({}, 2048);
    CHECK(s.rfind("EXT=\"JSON\" | EXT=\"CSS\" | EXT=\"PY\"", 0) == 0);
    CHECK(s.find("EXT=\"INF\"") != std::string::npos);
    auto shortS = c.buildDetectString({}, 30);
    CHECK(shortS.size() < 30);
    CHECK(shortS == "EXT=\"JSON\" | EXT=\"CSS\"");
    auto onlyPy = c.buildDetectString([](const LanguageDef& l) { return l.id == "python"; }, 2048);
    CHECK(onlyPy == "EXT=\"PY\" | EXT=\"PYW\"");
}

TEST_CASE("decoder: BOMs, UTF-8 and ANSI fallback") {
    auto d1 = decodeText(std::string("\xEF\xBB\xBFh\xC3\xA9", 6));
    CHECK(d1.encoding == Encoding::Utf8Bom);
    CHECK(d1.text == u"h\u00e9");
    auto d2 = decodeText(std::string("\xFF\xFEh\x00i\x00", 6));
    CHECK(d2.encoding == Encoding::Utf16LE);
    CHECK(d2.text == u"hi");
    auto d3 = decodeText(std::string("\xFE\xFF\x00h\x04\x10", 6));
    CHECK(d3.encoding == Encoding::Utf16BE);
    CHECK(d3.text == u"h\u0410");
    auto d4 = decodeText(std::string("plain \xE2\x82\xAC"));
    CHECK(d4.encoding == Encoding::Utf8);
    CHECK(d4.text == u"plain \u20AC");
    auto d5 = decodeText(std::string("caf\xE9"));  // Latin-1 / cp1252
    CHECK(d5.encoding == Encoding::Ansi);
    CHECK(d5.text == u"caf\u00e9");
    auto d6 = decodeText(std::string("x\xE9"), [](std::string_view) { return std::u16string(u"ANSI"); });
    CHECK(d6.text == u"ANSI");
    CHECK(isValidUtf8("\xF0\x9F\x98\x80"));
    CHECK_FALSE(isValidUtf8("\xC0\x80"));  // overlong
    CHECK_FALSE(isValidUtf8("\xED\xA0\x80"));  // surrogate
}

TEST_CASE("decoder: line ending normalisation") {
    CHECK(normalizeLineEndings(u"a\r\nb\nc\rd") == u"a\rb\rc\rd");
    CHECK(normalizeLineEndings(std::u16string(u"x\0y", 3)) == u"x y");
}

TEST_CASE("utf conversions round trip") {
    std::string s = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    CHECK(utf16ToUtf8(utf8ToUtf16(s)) == s);
    CHECK(utf8ToUtf16("\xF0\x9F\x98\x80").size() == 2);
}

TEST_CASE("theme: colors and overrides") {
    Color c;
    CHECK(parseColor("#0000FF", c));
    CHECK(c == 0x0000FF);
    CHECK(parseColor("abc", c));
    CHECK(c == 0xAABBCC);
    CHECK_FALSE(parseColor("#12345", c));

    auto t = Theme::builtin(false);
    std::vector<std::string> warn;
    t.apply(IniDocument::parse(std::string_view(
                "[View]\nbackground=#101010\nbogus=1\n"
                "[Tokens]\nkeyword=#112233,,bold+italic\nstring=,#FFEEDD\ncomment=zzz\nnope=#000000\n")),
            &warn);
    CHECK(t.background == 0x101010);
    CHECK(t.style(TokenClass::Keyword).fg == 0x112233);
    CHECK(t.style(TokenClass::Keyword).flags == (kBold | kItalic));
    CHECK(t.style(TokenClass::String).fg == Theme::builtin(false).style(TokenClass::String).fg);
    CHECK(t.style(TokenClass::String).bg.value() == 0xFFEEDD);
    CHECK(t.style(TokenClass::Comment).fg == Theme::builtin(false).style(TokenClass::Comment).fg);
    CHECK(warn.size() == 3);
}

TEST_CASE("theme: shipped sets resolve with per-language overrides") {
    for (bool dark : {false, true}) {
        const char* set = dark ? "dark" : "light";
        REQUIRE(fs::exists(kDist / "themes" / set / "default.theme"));
        auto base = loadTheme(kDist, set, "", dark);
        auto py = loadTheme(kDist, set, "python", dark);
        CHECK(base.background == (dark ? 0x1E1E1E : 0xFFFFFF));
        CHECK(py.style(TokenClass::Decorator).fg != base.style(TokenClass::Decorator).fg);
        auto ini = loadTheme(kDist, set, "ini", dark);
        CHECK(ini.style(TokenClass::SectionBracket).fg == ini.style(TokenClass::Punctuation).fg);
        CHECK(ini.style(TokenClass::Constant).fg != ini.style(TokenClass::Key).fg);
        CHECK(ini.style(TokenClass::Value).fg != ini.style(TokenClass::Key).fg);
        // Every shipped theme file must parse without warnings.
        for (auto& entry : fs::directory_iterator(kDist / "themes" / set)) {
            IniDocument doc;
            REQUIRE(IniDocument::loadFile(entry.path(), doc));
            std::vector<std::string> warn;
            Theme t = Theme::builtin(dark);
            t.apply(doc, &warn);
            CAPTURE(entry.path().string());
            CHECK(warn.empty());
        }
    }
}

TEST_CASE("shipped ini parses and matches defaults") {
    IniDocument doc;
    REQUIRE(IniDocument::loadFile(kDist / "tcl-syntaxhl.ini", doc));
    auto cfg = Config::fromIni(doc);
    auto def = Config::defaults();
    CHECK(cfg.tokenizer == def.tokenizer);
    CHECK(cfg.maxHighlightSize == def.maxHighlightSize);
    CHECK(cfg.useLazyHighlight == def.useLazyHighlight);
    CHECK(cfg.italicComments == def.italicComments);
    CHECK(cfg.theme == def.theme);
    CHECK(cfg.fontFace == def.fontFace);
    CHECK(cfg.fontSize == def.fontSize);
    REQUIRE(cfg.languages.size() == def.languages.size());
    for (size_t i = 0; i < def.languages.size(); ++i) {
        CHECK(cfg.languages[i].id == def.languages[i].id);
        CHECK(cfg.languages[i].extensions == def.languages[i].extensions);
    }
    CHECK_FALSE(cfg.languageForExtension("json")->options.getBool("support_jsonc_comments", true));
    CHECK_FALSE(cfg.languageForExtension("css")->options.getBool("support_scss_less", true));
}

TEST_CASE("lexer selection: native, regex, fallback") {
    auto cfg = Config::defaults();
    auto* json = cfg.languageForExtension("json");
    auto* py = cfg.languageForExtension("py");
    auto s1 = selectLexer(cfg, *json, kDist);
    CHECK(s1.lexer);
    CHECK(s1.kind == TokenizerKind::Native);
    CHECK(s1.notice.empty());

    cfg.tokenizer = TokenizerKind::Regex;
    auto s2 = selectLexer(cfg, *json, kDist);
    CHECK(s2.lexer);
    CHECK(s2.kind == TokenizerKind::Regex);
    auto s3 = selectLexer(cfg, *py, kDist);
    CHECK(s3.lexer);
    CHECK(s3.kind == TokenizerKind::Native);
    CHECK(s3.notice.find("python.rules") != std::string::npos);

    LanguageDef unknown;
    unknown.name = "Foo";
    unknown.id = unknown.lexerId = "foo";
    CHECK_FALSE(selectLexer(cfg, unknown, kDist).lexer);
    CHECK_FALSE(isLanguageAvailable(unknown, kDist));
    CHECK(isLanguageAvailable(*py, kDist));
}

TEST_CASE("settings: round trip keeps comments") {
    auto dir = fs::temp_directory_path() / "shl_settings_test";
    fs::create_directories(dir);
    auto p = dir / "test.ini";
    {
        std::ofstream f(p, std::ios::binary);
        f << "; my comment\n[General]\nfont_size=11\ncustom_key=keep\n[Languages]\nJSON=json\n";
    }
    Settings s;
    REQUIRE(s.load(p));
    CHECK(s.config().fontSize == 11);
    s.config().fontSize = 14;
    REQUIRE(s.save());
    Settings s2;
    REQUIRE(s2.load(p));
    CHECK(s2.config().fontSize == 14);
    CHECK(s2.document().get("General", "custom_key").value() == "keep");
    std::string raw;
    REQUIRE(readFileBytes(p, raw));
    CHECK(raw.find("; my comment") != std::string::npos);
    fs::remove_all(dir);
}
