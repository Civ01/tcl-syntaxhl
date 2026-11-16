#include "LazyModel.h"

#include <algorithm>

#include "TextUtil.h"

namespace shl {

LazyModel::LazyModel(std::u16string_view text, ILexer& lexer, size_t interval)
    : text_(text), lexer_(lexer), interval_(interval ? interval : 256) {
    lineStarts_.push_back(0);
    for (size_t i = 0; i < text.size(); ++i) {
        char16_t c = text[i];
        if (c == u'\r' || c == u'\n') {
            if (c == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n') ++i;
            if (i + 1 < text.size()) lineStarts_.push_back(i + 1);
        }
    }
    checkpoints_.push_back(LexState{});
    done_.assign(lineStarts_.size(), 0);
}

size_t LazyModel::lineStart(size_t line) const {
    return line < lineStarts_.size() ? lineStarts_[line] : text_.size();
}

size_t LazyModel::lineOfOffset(size_t offset) const {
    auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
    return it == lineStarts_.begin() ? 0 : static_cast<size_t>(it - lineStarts_.begin()) - 1;
}

void LazyModel::lexBlock(size_t block, LexState& state, std::vector<Token>* out) {
    size_t from = lineStart(block * interval_);
    size_t to = lineStart((block + 1) * interval_);
    std::vector<Token> scratch;
    auto& dst = out ? *out : scratch;
    size_t before = dst.size();
    lexer_.lex(text_.substr(from, to - from), state, dst);
    for (size_t i = before; i < dst.size(); ++i) dst[i].start += static_cast<uint32_t>(from);
}

void LazyModel::ensureCheckpoint(size_t block) {
    while (checkpoints_.size() <= block) {
        LexState st = checkpoints_.back();
        lexBlock(checkpoints_.size() - 1, st, nullptr);
        checkpoints_.push_back(st);
    }
}

std::vector<Token> LazyModel::takeTokens(size_t first, size_t last) {
    std::vector<Token> result;
    if (lineStarts_.empty()) return result;
    last = std::min(last, lineStarts_.size() - 1);
    while (first <= last && done_[first]) ++first;
    while (last > first && done_[last]) --last;
    if (first > last) return result;

    size_t firstBlock = first / interval_, lastBlock = last / interval_;
    ensureCheckpoint(firstBlock);
    size_t lo = lineStart(first), hi = lineStart(last + 1);
    std::vector<Token> tmp;
    for (size_t b = firstBlock; b <= lastBlock; ++b) {
        LexState st = checkpoints_[b];
        tmp.clear();
        lexBlock(b, st, &tmp);
        if (checkpoints_.size() == b + 1) checkpoints_.push_back(st);
        for (const auto& t : tmp)
            if (t.start >= lo && t.start < hi) result.push_back(t);
    }
    std::fill(done_.begin() + static_cast<ptrdiff_t>(first), done_.begin() + static_cast<ptrdiff_t>(last) + 1, 1);
    return result;
}

}  // namespace shl
