#include "Tokenizer.h"

#include "UnicodeText.h"

namespace paper_check {
namespace {

// 「收尾」动作在四处都要做，抽成函数避免漏掉其中一处 —— 漏了会吞掉文本末尾。
void FlushCjkRun(TokenList& tokens, std::vector<ByteString>& current_run, TokenMode mode) {
    if (current_run.empty()) {
        return;
    }

    if (current_run.size() == 1) {
        // 单字成段时保留它，否则被标点切碎的短句会一个词元都取不到。
        tokens.push_back(current_run.front());
    } else if (mode == TokenMode::Bigram) {
        for (std::size_t index = 0; index + 1 < current_run.size(); ++index) {
            tokens.push_back(current_run[index] + current_run[index + 1]);
        }
    } else {
        for (const ByteString& single_character : current_run) {
            tokens.push_back(single_character);
        }
    }

    current_run.clear();
}

void FlushAsciiWord(TokenList& tokens, ByteString& current_word) {
    if (!current_word.empty()) {
        tokens.push_back(current_word);
        current_word.clear();
    }
}

}  // namespace

Tokenizer::Tokenizer(TokenMode mode) noexcept : mode_(mode) {}

TokenList Tokenizer::Split(const CodePointList& code_points) const {
    TokenList tokens;
    tokens.reserve(code_points.size());

    std::vector<ByteString> current_cjk_run;
    ByteString current_ascii_word;

    for (const CodePoint code_point : code_points) {
        if (unicode::IsCjk(code_point)) {
            FlushAsciiWord(tokens, current_ascii_word);
            current_cjk_run.push_back(unicode::Encode(code_point));
        } else if (unicode::IsAsciiAlphaNumeric(code_point)) {
            FlushCjkRun(tokens, current_cjk_run, mode_);
            // 统一折成小写，避免 "The" 和 "the" 被当成两个不同的词。
            const bool is_upper_case = (code_point >= U'A' && code_point <= U'Z');
            const CodePoint normalized = is_upper_case ? (code_point - U'A' + U'a') : code_point;
            current_ascii_word.push_back(static_cast<char>(normalized));
        } else {
            FlushCjkRun(tokens, current_cjk_run, mode_);
            FlushAsciiWord(tokens, current_ascii_word);
        }
    }

    FlushCjkRun(tokens, current_cjk_run, mode_);
    FlushAsciiWord(tokens, current_ascii_word);
    return tokens;
}

}  // namespace paper_check
