// Settings model backing the (future) runtime configuration window.
// Keeps the parsed IniDocument so that saving preserves comments, ordering and
// unknown keys; only values the model owns are rewritten.
#pragma once

#include <filesystem>

#include "Config.h"
#include "IniDocument.h"

namespace shl {

class Settings {
public:
    bool load(const std::filesystem::path& iniPath, const AnsiConverter& ansi = {}) {
        path_ = iniPath;
        bool ok = IniDocument::loadFile(iniPath, doc_, ansi);
        config_ = ok ? Config::fromIni(doc_) : Config::defaults();
        return ok;
    }

    bool save() {
        config_.writeGeneral(doc_);
        for (const auto& lang : config_.languages) {
            std::string exts;
            for (const auto& e : lang.extensions) exts += (exts.empty() ? "" : ",") + e;
            doc_.set("Languages", lang.name, exts);
            for (const auto& [k, v] : lang.options.values) doc_.set(lang.name, k, v);
        }
        return doc_.saveFile(path_);
    }

    Config& config() { return config_; }
    const Config& config() const { return config_; }
    const IniDocument& document() const { return doc_; }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
    IniDocument doc_;
    Config config_ = Config::defaults();
};

}  // namespace shl
