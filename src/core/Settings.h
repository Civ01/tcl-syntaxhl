// Settings model behind the settings window.
//
// Holds the parsed IniDocument plus a Config that the UI edits. save() writes
// only what changed (values compared in canonical form, so "2M" == "2097152"),
// keeping comments, ordering and unknown keys intact.
#pragma once

#include <filesystem>
#include <string>

#include "Config.h"
#include "IniDocument.h"

namespace shl {

class Settings {
public:
    // Returns false if the file could not be read (defaults are used then and
    // save() creates the file).
    bool load(const std::filesystem::path& iniPath, const AnsiConverter& ansi = {});

    // Applies the edited config to the document and writes the file.
    bool save();
    // Applies the edited config to the document without writing (for tests).
    void applyToDocument();

    Config& config() { return config_; }
    const Config& config() const { return config_; }
    const IniDocument& document() const { return doc_; }
    const std::filesystem::path& path() const { return path_; }

private:
    void applyGeneral();
    void applyLanguages();

    std::filesystem::path path_;
    IniDocument doc_;
    Config config_ = Config::defaults();
};

}  // namespace shl
