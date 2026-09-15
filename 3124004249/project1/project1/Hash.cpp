#include "Hash.h"

namespace paper_check {

std::uint64_t Fnv1a64(const ByteString& key) noexcept {
    // 两个常数来自 FNV 规范，必须原样使用。
    const std::uint64_t kOffsetBasis = 0xCBF29CE484222325ULL;
    const std::uint64_t kPrime = 0x00000100000001B3ULL;

    std::uint64_t hash = kOffsetBasis;
    for (const char character : key) {
        // 必须先转 unsigned char：char 在 MSVC 下是 signed，字节大于 127 时会符号扩展，
        // 把高 32 位污染成 1。
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(character));
        hash *= kPrime;  // uint64 溢出即按 2^64 取模，正是 FNV 期望的行为
    }
    return hash;
}

}  // namespace paper_check
