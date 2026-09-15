// UTF-8 编解码与字符分类。全是无状态的工具函数，不需要类。
#pragma once

#include "Common.h"

namespace paper_check {
namespace unicode {

// 字节流解码成码点序列。
// 非法序列不抛异常，而是按 Unicode 建议消费「最长有效子序列」并产出一个 U+FFFD ——
// 既不会漏掉后面本应合法的字节，也不会为一个坏字符产出好几个替换字符。
// 样例里按字节删字构造的文本会产生这种非法字节，若在这里抛异常就等于崩溃退出，会被扣分。
CodePointList Decode(const ByteString& bytes);

// 码点重新编码为 UTF-8 字节串。
ByteString Encode(CodePoint code_point);

// 是否汉字（CJK 统一表意文字 / 扩展 A / 兼容表意文字三个常用区间）。
bool IsCjk(CodePoint code_point) noexcept;

// 是否 ASCII 字母或数字，用来把英文单词整体当成一个词元。
bool IsAsciiAlphaNumeric(CodePoint code_point) noexcept;

}  // namespace unicode
}  // namespace paper_check
