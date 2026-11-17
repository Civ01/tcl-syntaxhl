#include "IniDocument.h"

#include <algorithm>
#include <fstream>

#include "TextUtil.h"

namespace shl {

bool readFileBytes(const std::filesystem::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto size = f.tellg();
    if (size < 0) return false;
    out.resize(static_cast<size_t>(size));
    f.seekg(0, std::ios::beg);
    if (size > 0 && !f.read(out.data(), size)) return false;
    return true;
}

IniDocument IniDocument::parse(std::u16string_view text) { return parse(utf16ToUtf8(text)); }

IniDocument IniDocument::parse(std::string_view s) {
    IniDocument doc;
    std::string current;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t eol = s.find_first_of("\r\n", pos);
        if (eol == std::string_view::npos) eol = s.size();
        std::string_view raw = s.substr(pos, eol - pos);
        pos = eol;
        if (pos < s.size() && s[pos] == '\r') ++pos;
        if (pos < s.size() && s[pos] == '\n') ++pos;

        Line line;
        line.raw = std::string(raw);
        line.section = current;
        auto t = trim(raw);
        if (t.empty()) {
            line.type = LineType::Blank;
        } else if (t[0] == ';' || t[0] == '#') {
            line.type = LineType::Comment;
        } else if (t[0] == '[') {
            auto close = t.find(']');
            if (close != std::string_view::npos) {
                current = std::string(trim(t.substr(1, close - 1)));
                line.type = LineType::Section;
                line.section = current;
            }
        } else {
            auto eq = t.find('=');
            if (eq != std::string_view::npos && eq > 0) {
                line.type = LineType::KeyValue;
                line.key = std::string(trim(t.substr(0, eq)));
                line.value = std::string(trim(t.substr(eq + 1)));
            }
        }
        doc.lines_.push_back(std::move(line));
    }
    return doc;
}

bool IniDocument::loadFile(const std::filesystem::path& p, IniDocument& out, const AnsiConverter& ansi) {
    std::string bytes;
    out = IniDocument();
    if (!readFileBytes(p, bytes)) return false;
    auto decoded = decodeText(bytes, ansi);
    out = parse(std::u16string_view(decoded.text));
    return true;
}

std::optional<std::string> IniDocument::get(std::string_view section, std::string_view key) const {
    for (const auto& l : lines_)
        if (l.type == LineType::KeyValue && iequals(l.section, section) && iequals(l.key, key)) return l.value;
    return std::nullopt;
}

std::string IniDocument::getOr(std::string_view section, std::string_view key, std::string_view def) const {
    auto v = get(section, key);
    return v ? *v : std::string(def);
}

std::vector<IniDocument::Entry> IniDocument::entries(std::string_view section) const {
    std::vector<Entry> out;
    for (const auto& l : lines_)
        if (l.type == LineType::KeyValue && iequals(l.section, section)) out.emplace_back(l.key, l.value);
    return out;
}

std::vector<std::string> IniDocument::sections() const {
    std::vector<std::string> out;
    for (const auto& l : lines_) {
        if (l.type != LineType::Section) continue;
        bool seen = false;
        for (const auto& s : out)
            if (iequals(s, l.section)) seen = true;
        if (!seen) out.push_back(l.section);
    }
    return out;
}

bool IniDocument::hasSection(std::string_view section) const {
    for (const auto& l : lines_)
        if ((l.type == LineType::Section || l.type == LineType::KeyValue) && iequals(l.section, section)) return true;
    return false;
}

void IniDocument::set(std::string_view section, std::string_view key, std::string_view value) {
    for (auto& l : lines_) {
        if (l.type == LineType::KeyValue && iequals(l.section, section) && iequals(l.key, key)) {
            l.value = std::string(value);
            return;
        }
    }
    Line kv;
    kv.type = LineType::KeyValue;
    kv.section = std::string(section);
    kv.key = std::string(key);
    kv.value = std::string(value);
    // Insert after the last key/value (or header) of that section.
    ptrdiff_t insertAt = -1;
    for (size_t i = 0; i < lines_.size(); ++i) {
        const auto& l = lines_[i];
        if (iequals(l.section, section) && (l.type == LineType::KeyValue || l.type == LineType::Section))
            insertAt = static_cast<ptrdiff_t>(i) + 1;
    }
    if (insertAt < 0) {
        if (!section.empty()) {
            if (!lines_.empty() && lines_.back().type != LineType::Blank) { Line blank; blank.type = LineType::Blank; lines_.push_back(blank); }
            Line hdr;
            hdr.type = LineType::Section;
            hdr.section = std::string(section);
            lines_.push_back(hdr);
        }
        lines_.push_back(kv);
    } else {
        lines_.insert(lines_.begin() + insertAt, kv);
    }
}

bool IniDocument::remove(std::string_view section, std::string_view key) {
    size_t before = lines_.size();
    lines_.erase(std::remove_if(lines_.begin(), lines_.end(),
                                [&](const Line& l) {
                                    return l.type == LineType::KeyValue && iequals(l.section, section) &&
                                           iequals(l.key, key);
                                }),
                 lines_.end());
    return lines_.size() != before;
}

bool IniDocument::removeSection(std::string_view section) {
    size_t before = lines_.size();
    std::vector<Line> out;
    out.reserve(lines_.size());
    bool inside = false;
    for (auto& l : lines_) {
        if (l.type == LineType::Section) inside = iequals(l.section, section);
        if (!inside) out.push_back(std::move(l));
    }
    // Drop a blank line left directly before where the section was at the end.
    while (out.size() > 1 && out.back().type == LineType::Blank && out[out.size() - 2].type == LineType::Blank)
        out.pop_back();
    lines_ = std::move(out);
    return lines_.size() != before;
}

std::string IniDocument::serialize() const {
    std::string out;
    for (const auto& l : lines_) {
        switch (l.type) {
            case LineType::Section: out += "[" + l.section + "]"; break;
            case LineType::KeyValue: out += l.key + "=" + l.value; break;
            default: out += l.raw; break;
        }
        out += "\r\n";
    }
    return out;
}

bool IniDocument::saveFile(const std::filesystem::path& p) const {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    auto s = serialize();
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
    return static_cast<bool>(f);
}

}  // namespace shl
