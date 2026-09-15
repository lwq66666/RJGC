#include "Tokenizer.h"

#include <utility>

#include "UnicodeText.h"

namespace paper_check {
namespace {

// 遍历所有词元，每产生一个就调一次 emit。
// 用模板而不是 std::function：模板能在编译期内联 emit，没有间接调用开销。
// Split 和 CountTokens 都建立在这一份遍历逻辑上，避免两份容易走样的实现。
//
// 【为什么用流式而不是「先把一段汉字收集起来再切」】
// 收集需要额外的 vector<string>（大文本下是上百万个元素）。改成流式：
// 记住上一个汉字，每来一个新汉字就吐出「上一个 + 当前」这个二元组；
// 整段长度用 run_length 记着，这样既能识别长度为 1 的段，又不需要缓冲。
template <typename EmitToken>
void ForEachToken(const CodePointList& code_points, TokenMode mode, EmitToken emit) {
    ByteString previous_cjk;
    bool has_previous = false;
    std::size_t current_run_length = 0;
    ByteString ascii_word;

    auto close_cjk_run = [&]() {
        // 整段只有一个汉字时它无法和谁组成二元组，就把自己当作一个词元。
        // 少这一步的话，被标点切碎的短句会一个词元都取不到。
        if (mode == TokenMode::Bigram && current_run_length == 1) {
            emit(previous_cjk);
        }
        previous_cjk.clear();
        has_previous = false;
        current_run_length = 0;
    };

    auto close_ascii_word = [&]() {
        if (!ascii_word.empty()) {
            emit(ascii_word);
            ascii_word.clear();
        }
    };

    for (const CodePoint code_point : code_points) {
        if (unicode::IsCjk(code_point)) {
            close_ascii_word();

            ByteString current = unicode::Encode(code_point);
            if (mode == TokenMode::Unigram) {
                emit(current);
            } else {
                if (has_previous) {
                    emit(previous_cjk + current);
                }
                previous_cjk = std::move(current);  // 下一个汉字还要用，留一份
                has_previous = true;
            }
            ++current_run_length;
        } else if (unicode::IsAsciiAlphaNumeric(code_point)) {
            close_cjk_run();
            // 统一折成小写，避免 "The" 和 "the" 被当成两个不同的词。
            const bool is_upper_case = (code_point >= U'A' && code_point <= U'Z');
            const CodePoint normalized = is_upper_case ? (code_point - U'A' + U'a') : code_point;
            ascii_word.push_back(static_cast<char>(normalized));
        } else {
            close_cjk_run();
            close_ascii_word();
        }
    }

    // 循环结束时最后一段还在缓冲区里，必须收尾，否则文本末尾会凭空消失。
    close_cjk_run();
    close_ascii_word();
}

}  // namespace

Tokenizer::Tokenizer(TokenMode mode) noexcept : mode_(mode) {}

TokenList Tokenizer::Split(const CodePointList& code_points) const {
    TokenList tokens;
    tokens.reserve(code_points.size());
    ForEachToken(code_points, mode_,
                 [&tokens](const ByteString& token) { tokens.push_back(token); });
    return tokens;
}

TermFrequency Tokenizer::CountTokens(const CodePointList& code_points) const {
    TermFrequency frequencies;
    // 预留一些桶减少 rehash。唯一词元数不会超过码点数，按下界估一个即可。
    frequencies.reserve(code_points.size() / 4 + 16);
    ForEachToken(code_points, mode_,
                 [&frequencies](const ByteString& token) { ++frequencies[token]; });
    return frequencies;
}

}  // namespace paper_check
