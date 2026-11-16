// Incremental (visible-area) tokenization for large files.
//
// Keeps the lexer state at every `interval`-th line start (checkpoints, built
// forward on demand) and remembers which lines have already been tokenized,
// so each line is produced at most once.
#pragma once

#include <string_view>
#include <vector>

#include "Token.h"

namespace shl {

class LazyModel {
public:
    LazyModel(std::u16string_view text, ILexer& lexer, size_t interval = 256);

    size_t lineCount() const { return lineStarts_.size(); }
    size_t lineStart(size_t line) const;  // line >= lineCount() -> text size
    size_t lineOfOffset(size_t offset) const;

    // Tokens (absolute offsets) for lines [first, last] that were not produced
    // before; marks them as produced. Returns empty if nothing new.
    std::vector<Token> takeTokens(size_t firstLine, size_t lastLine);

    bool isLineDone(size_t line) const { return line < done_.size() && done_[line]; }

private:
    void ensureCheckpoint(size_t block);
    void lexBlock(size_t block, LexState& state, std::vector<Token>* out);

    std::u16string_view text_;
    ILexer& lexer_;
    size_t interval_;
    std::vector<size_t> lineStarts_;
    std::vector<LexState> checkpoints_;  // state at line block*interval_
    std::vector<char> done_;
};

}  // namespace shl
