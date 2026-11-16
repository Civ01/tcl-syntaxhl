// Minimal, round-trippable INI document (UTF-8 strings).
//
// - Sections and keys are case-insensitive; order and duplicates are kept.
// - Only full-line comments ("; ..." or "# ...") are recognised, so values may
//   contain ';' and '#' (needed for colors and regexes).
// - Keys before the first section belong to section "".
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "TextDecoder.h"

namespace shl {

class IniDocument {
public:
    using Entry = std::pair<std::string, std::string>;

    static IniDocument parse(std::string_view utf8);
    static IniDocument parse(std::u16string_view text);
    // Returns false if the file cannot be read (document is left empty).
    static bool loadFile(const std::filesystem::path& p, IniDocument& out, const AnsiConverter& ansi = {});

    std::optional<std::string> get(std::string_view section, std::string_view key) const;
    std::string getOr(std::string_view section, std::string_view key, std::string_view def) const;
    std::vector<Entry> entries(std::string_view section) const;
    std::vector<std::string> sections() const;  // in order, as written
    bool hasSection(std::string_view section) const;

    // Updates the first occurrence of key, or appends it to the section
    // (creating the section at the end of the document if needed).
    void set(std::string_view section, std::string_view key, std::string_view value);

    std::string serialize() const;  // CRLF line endings
    bool saveFile(const std::filesystem::path& p) const;

private:
    enum class LineType { Blank, Comment, Section, KeyValue, Other };
    struct Line {
        LineType type = LineType::Other;
        std::string raw;      // original text for Blank/Comment/Other
        std::string section;  // section name (for Section lines: the name itself)
        std::string key;
        std::string value;
    };
    std::vector<Line> lines_;
};

bool readFileBytes(const std::filesystem::path& p, std::string& out);

}  // namespace shl
