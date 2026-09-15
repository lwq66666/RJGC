// UTF-8 编解码与字符分类。全是无状态的工具函数，不需要类。
#pragma once

#include "Common.h"

namespace paper_check {
namespace unicode {

// 字节流解码成码点序列。
// 非法序列不抛异常，压入 U+FFFD 后只前进 1 字节 —— 这样每个字节都会被检查到，
// 不会漏掉乱码后面本应合法的字符。样例里按字节删字构造的文本会产生非法字节，
// 若在这里抛异常就等于崩溃退出，会被扣分。
CodePointList Decode(const ByteString& bytes);

// 码点重新编码为 UTF-8 字节串。
ByteString Encode(CodePoint code_point);

// 是否汉字（CJK 统一表意文字 / 扩展 A / 兼容表意文字三个常用区间）。
bool IsCjk(CodePoint code_point) noexcept;

// 是否 ASCII 字母或数字，用来把英文单词整体当成一个词元。
bool IsAsciiAlphaNumeric(CodePoint code_point) noexcept;

}  // namespace unicode
}  // namespace paper_check
