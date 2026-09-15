// 分词器：把码点序列切成词元列表。不碰文件、不做哈希。
#pragma once

#include "Common.h"

namespace paper_check {

// 用枚举而不是 bool，调用点写成 Tokenizer(TokenMode::Bigram) 比 Tokenizer(true) 清楚得多。
enum class TokenMode {
    // 相邻两字组合（默认）。唯一能识别「乱序型抄袭」的口径。
    Bigram,
    // 单字。保留它只为让单元测试能显式钉住这个坑：打乱字序不改变单字词频，
    // 两篇文本的指纹会完全相同，重复率被误报成 1.00。
    Unigram,
};

class Tokenizer {
public:
    explicit Tokenizer(TokenMode mode = TokenMode::Bigram) noexcept;

    // 连续汉字按模式切分（单字成段时保留该字，否则整段会被丢掉）；
    // 连续字母数字折成小写作为一个词元；其余（标点、空白）一律作分隔符丢弃。
    TokenList Split(const CodePointList& code_points) const;

    TokenMode Mode() const noexcept { return mode_; }

private:
    TokenMode mode_;
};

}  // namespace paper_check
