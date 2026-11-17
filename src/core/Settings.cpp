#include "Settings.h"

#include "OptionSchema.h"
#include "TextUtil.h"

namespace shl {

namespace {
const char* kGeneral = "General";
const char* kLanguages = "Languages";

std::string joinExtensions(const std::vector<std::string>& exts) {
    std::string s;
    for (const auto& e : exts) s += (s.empty() ? "" : ",") + e;
    return s;
}

std::vector<std::string> normalizedExtensions(std::string_view list) {
    std::vector<std::string> out;
    for (auto& e : splitList(list, ',')) {
        auto x = toLowerAscii(e);
        while (!x.empty() && (x[0] == '.' || x[0] == '*')) x.erase(0, 1);
        if (!x.empty()) out.push_back(x);
    }
    return out;
}

bool sameLanguages(const std::vector<LanguageDef>& a, const std::vector<LanguageDef>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!iequals(a[i].name, b[i].name) || a[i].extensions != b[i].extensions) return false;
    return true;
}

// Known option definition for a key in a language section (override or lexer option).
const OptionDef* languageOptionDef(const LanguageDef& lang, std::string_view key) {
    for (const auto& d : languageOverrideOptions())
        if (iequals(key, d.key)) return &d;
    for (const auto& d : lexerOptions(lang.lexerId))
        if (iequals(key, d.key)) return &d;
    return nullptr;
}
}  // namespace

bool Settings::load(const std::filesystem::path& iniPath, const AnsiConverter& ansi) {
    path_ = iniPath;
    bool ok = IniDocument::loadFile(iniPath, doc_, ansi);
    config_ = ok ? Config::fromIni(doc_) : Config::defaults();
    return ok;
}

void Settings::applyGeneral() {
    for (const auto& def : generalOptions()) {
        std::string value = getGeneralValue(config_, def.key);
        auto old = doc_.get(kGeneral, def.key);
        if (old ? def.sameValue(*old, value) : def.sameValue(def.defaultValue, value)) continue;
        doc_.set(kGeneral, def.key, value);
    }
}

void Settings::applyLanguages() {
    const auto& langs = config_.languages;
    bool hadSection = doc_.hasSection(kLanguages);
    if (!hadSection && sameLanguages(langs, Config::defaults().languages)) {
        // [Languages] absent and unchanged: keep relying on the defaults.
    } else {
        auto existing = doc_.entries(kLanguages);
        // Removed languages: drop their line and their option section.
        for (const auto& [name, exts] : existing) {
            bool keep = false;
            for (const auto& l : langs) keep |= iequals(l.name, name);
            if (!keep) {
                doc_.remove(kLanguages, name);
                doc_.removeSection(name);
            }
        }
        if (!hadSection) {
            // Section is created on first set(); write all languages in order.
        }
        for (const auto& l : langs) {
            auto old = doc_.get(kLanguages, l.name);
            if (old && normalizedExtensions(*old) == l.extensions) continue;
            doc_.set(kLanguages, l.name, joinExtensions(l.extensions));
        }
    }
    // Per-language option sections: set changed keys, remove deleted ones.
    for (const auto& l : langs) {
        for (const auto& [key, value] : l.options.values) {
            auto old = doc_.get(l.name, key);
            if (old) {
                const OptionDef* def = languageOptionDef(l, key);
                bool same = def ? def->sameValue(*old, value) : trim(*old) == trim(value);
                if (same) continue;
            }
            doc_.set(l.name, key, value);
        }
        for (const auto& [key, value] : doc_.entries(l.name)) {
            (void)value;
            if (!l.options.values.count(toLowerAscii(key))) doc_.remove(l.name, key);
        }
    }
}

void Settings::applyToDocument() {
    applyGeneral();
    applyLanguages();
}

bool Settings::save() {
    applyToDocument();
    return doc_.saveFile(path_);
}

}  // namespace shl
