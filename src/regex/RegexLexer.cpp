#include "RegexLexer.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <regex>

#include "core/Config.h"
#include "core/TextUtil.h"
#include "lexers/LineLexer.h"

namespace shl {
namespace {

constexpr uint32_t kMaxStack = 8;
constexpr int kMaxZeroLengthSteps = 4;

std::wstring toWide(std::u16string_view s) {
    std::wstring w;
    w.reserve(s.size());
    for (char16_t c : s) w.push_back(static_cast<wchar_t>(c));
    return w;
}

struct Rule {
    int order = 0;
    TokenClass cls = TokenClass::Default;
    std::wregex re;
    int push = -1;  // target state index
    bool pop = false;
    bool hasPattern = false;
};

struct State {
    std::string name;
    TokenClass defaultCls = TokenClass::Default;
    bool popAtEol = false;  // eol_pop=true: state ends with the line
    std::vector<Rule> rules;
};

class RegexLexer final : public LineLexer {
public:
    explicit RegexLexer(std::vector<State> states) : states_(std::move(states)) {}

protected:
    void lexLine(std::u16string_view line, LexState& st, Emitter& em) override {
        std::wstring w = toWide(line);
        size_t pos = 0, n = w.size();
        int zeroSteps = 0;
        while (pos < n) {
            const State& state = states_[std::min<size_t>(st.mode, states_.size() - 1)];
            bool matched = false;
            for (const auto& r : state.rules) {
                std::wsmatch m;
                auto flags = std::regex_constants::match_continuous;
                if (pos > 0) flags |= std::regex_constants::match_prev_avail;
                if (!std::regex_search(w.cbegin() + static_cast<ptrdiff_t>(pos), w.cend(), m, r.re, flags)) continue;
                size_t len = static_cast<size_t>(m.length(0));
                bool changesState = r.push >= 0 || r.pop;
                if (len == 0 && (!changesState || zeroSteps >= kMaxZeroLengthSteps)) continue;
                em.add(pos, len, r.cls);
                if (r.pop) pop(st);
                if (r.push >= 0) push(st, static_cast<uint32_t>(r.push));
                zeroSteps = len == 0 ? zeroSteps + 1 : 0;
                pos += len;
                matched = true;
                break;
            }
            if (!matched) {
                em.add(pos, 1, state.defaultCls);
                ++pos;
                zeroSteps = 0;
            }
        }
    }

    void endOfLine(LexState& st) override {
        for (int guard = 0; guard < 16; ++guard) {
            const State& s = states_[std::min<size_t>(st.mode, states_.size() - 1)];
            if (!s.popAtEol || (st.mode == 0 && st.depth == 0)) break;
            pop(st);
        }
    }

private:
    // mode = current state; extra holds up to 8 saved states (8 bits each).
    static void push(LexState& st, uint32_t target) {
        if (st.depth < kMaxStack) {
            uint64_t shift = 8 * st.depth;
            st.extra = (st.extra & ~(uint64_t(0xFF) << shift)) | (uint64_t(st.mode & 0xFF) << shift);
        }
        ++st.depth;
        st.mode = target;
    }
    static void pop(LexState& st) {
        if (st.depth == 0) { st.mode = 0; return; }
        --st.depth;
        st.mode = st.depth < kMaxStack ? static_cast<uint32_t>((st.extra >> (8 * st.depth)) & 0xFF) : 0;
    }

    std::vector<State> states_;
};

}  // namespace

RulesParseResult parseRules(const IniDocument& doc) {
    RulesParseResult res;
    res.name = doc.getOr("Meta", "name", "");

    // Collect state sections: [Rules] is "main" (index 0), [Rules.x] others.
    std::vector<std::string> sectionNames;
    std::vector<State> states;
    State mainState;
    mainState.name = "main";
    states.push_back(mainState);
    sectionNames.push_back("Rules");
    for (const auto& sec : doc.sections()) {
        if (sec.size() > 6 && iequals(sec.substr(0, 6), "Rules.")) {
            State s;
            s.name = toLowerAscii(sec.substr(6));
            states.push_back(s);
            sectionNames.push_back(sec);
        }
    }
    if (states.size() > 255) {
        res.error = "too many states";
        return res;
    }
    auto findState = [&](std::string_view name) -> int {
        auto n = toLowerAscii(trim(name));
        for (size_t i = 0; i < states.size(); ++i)
            if (states[i].name == n) return static_cast<int>(i);
        return -1;
    };

    for (size_t si = 0; si < states.size(); ++si) {
        std::map<int, Rule> rules;
        for (const auto& [key, value] : doc.entries(sectionNames[si])) {
            if (iequals(key, "eol_pop")) {
                if (si == 0) {
                    res.error = "[Rules] eol_pop is only valid in [Rules.<state>] sections";
                    return res;
                }
                states[si].popAtEol = parseBool(value, false);
                continue;
            }
            if (iequals(key, "default")) {
                if (!parseTokenClass(value, states[si].defaultCls)) {
                    res.error = "[" + sectionNames[si] + "] unknown class '" + value + "'";
                    return res;
                }
                continue;
            }
            auto dot = key.find('.');
            std::string num = key.substr(0, dot);
            char* endp = nullptr;
            long order = std::strtol(num.c_str(), &endp, 10);
            if (num.empty() || *endp != '\0') {
                res.error = "[" + sectionNames[si] + "] bad rule key '" + key + "'";
                return res;
            }
            Rule& r = rules[static_cast<int>(order)];
            r.order = static_cast<int>(order);
            if (dot != std::string::npos) {
                auto attr = toLowerAscii(key.substr(dot + 1));
                if (attr == "push") {
                    r.push = findState(value);
                    if (r.push < 0) {
                        res.error = "[" + sectionNames[si] + "] unknown state '" + value + "'";
                        return res;
                    }
                } else if (attr == "pop") {
                    r.pop = parseBool(value, false);
                } else {
                    res.error = "[" + sectionNames[si] + "] unknown attribute '" + key + "'";
                    return res;
                }
                continue;
            }
            auto colon = value.find(':');
            if (colon == std::string::npos) {
                res.error = "[" + sectionNames[si] + "] rule " + key + " needs 'class:regex'";
                return res;
            }
            if (!parseTokenClass(value.substr(0, colon), r.cls)) {
                res.error = "[" + sectionNames[si] + "] unknown class '" + value.substr(0, colon) + "'";
                return res;
            }
            try {
                r.re = std::wregex(toWide(utf8ToUtf16(value.substr(colon + 1))),
                                   std::regex_constants::ECMAScript | std::regex_constants::optimize);
                r.hasPattern = true;
            } catch (const std::regex_error& e) {
                res.error = "[" + sectionNames[si] + "] rule " + key + ": invalid regex (" + e.what() + ")";
                return res;
            }
        }
        for (auto& [order, r] : rules) {
            if (!r.hasPattern) {
                res.error = "[" + sectionNames[si] + "] rule " + std::to_string(order) + " has no 'class:regex'";
                return res;
            }
            states[si].rules.push_back(std::move(r));
        }
    }
    if (states[0].rules.empty()) {
        res.error = "no rules in [Rules]";
        return res;
    }
    res.lexer = std::make_unique<RegexLexer>(std::move(states));
    return res;
}

}  // namespace shl
