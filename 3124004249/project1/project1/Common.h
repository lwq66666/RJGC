// 全局类型别名与常量：让「字节 / 码点 / 词元 / 指纹」在类型层面就分得清。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace paper_check {

// 一个 Unicode 码点（汉字「今」= U+4ECA）。注意它和「一个字节 char」不是一回事。
using CodePoint = char32_t;
using CodePointList = std::vector<CodePoint>;

using ByteString = std::string;  // 一段原始字节，可能是多字节 UTF-8
using Token = ByteString;        // 一个词元，用字节串表示，方便当 map 的 key
using TokenList = std::vector<Token>;

// 词元 -> 出现次数。用 unordered_map 而不是 map：这里不需要有序，
// 而且哈希表的插入更快。
using TermFrequency = std::unordered_map<Token, std::int64_t>;

using Fingerprint = std::uint64_t;  // SimHash 指纹

constexpr int kDefaultFingerprintBits = 64;
constexpr CodePoint kReplacementCharacter = 0xFFFD;  // U+FFFD 替换字符

}  // namespace paper_check
