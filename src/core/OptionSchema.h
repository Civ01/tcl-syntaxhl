// Declarative description of every configurable option (portable).
//
// The settings window builds its controls from this schema and Settings uses
// it to decide whether a value really changed. Adding a lexer option means
// adding one entry to lexerOptions() (and reading it in the lexer factory).
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Config.h"

namespace shl {

enum class OptionType {
    Bool,      // true/false
    Int,       // integer in [minValue, maxValue]
    Size,      // byte size with optional K/M/G suffix
    Enum,      // one of `choices`
    ThemeSet,  // folder under themes/ (choices may add "auto")
    Font,      // font face name
};

struct OptionDef {
    const char* key;          // INI key (snake_case)
    OptionType type;
    const char* label;        // UI label
    const char* help;         // one-line description (tooltip / INI comment)
    const char* defaultValue; // canonical default
    int minValue = 0, maxValue = 0;         // Int
    std::vector<const char*> choices = {};  // Enum (and ThemeSet extras)

    // Canonical form of a value ("2097152" -> "2M", "yes" -> "true"), or empty
    // if the value is invalid for this option.
    std::string normalize(std::string_view value) const;
    bool sameValue(std::string_view a, std::string_view b) const;
};

// [General] options, in display order.
const std::vector<OptionDef>& generalOptions();
const OptionDef* findGeneralOption(std::string_view key);

// Reads / writes a [General] option of a Config as its canonical string.
std::string getGeneralValue(const Config& cfg, std::string_view key);
bool setGeneralValue(Config& cfg, std::string_view key, std::string_view value);  // false if invalid

// Per-language overrides valid for every language section ("" = inherit).
const std::vector<OptionDef>& languageOverrideOptions();

// Options understood by one native lexer (e.g. "json" -> support_jsonc_comments).
const std::vector<OptionDef>& lexerOptions(std::string_view lexerId);

}  // namespace shl
