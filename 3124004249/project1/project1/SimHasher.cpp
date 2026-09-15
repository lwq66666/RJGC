#include "SimHasher.h"

#include <cmath>
#include <stdexcept>
#include <unordered_map>

#include "Exceptions.h"
#include "Hash.h"

namespace paper_check {
namespace {

// 用 SWAR 位技巧数 64 位整数里 1 的个数。不用 __popcnt64 / __builtin_popcountll
// 是为了避免一堆 #ifdef，跨编译器行为也一致。
int PopCount64(std::uint64_t value) noexcept {
    value = value - ((value >> 1) & 0x5555555555555555ULL);
    value = (value & 0x3333333333333333ULL) + ((value >> 2) & 0x3333333333333333ULL);
    value = (value + (value >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return static_cast<int>((value * 0x0101010101010101ULL) >> 56);
}

// 数末尾 0 的个数（count trailing zeros），也就是最低的那个 1 在第几位。
// 同样用「查表 + 一次乘法」的可移植写法代替 _BitScanForward64 / __builtin_ctzll：
// 先把最低位的 1 单独取出来，再乘一个 de Bruijn 常数把不同的位模式映射到
// 高 6 位的不同取值上，就能直接查表。
// 前置条件：value != 0。
int CountTrailingZeros64(std::uint64_t value) noexcept {
    // 这张 64 项的表与下面的常数是配套的，改一个就必须重算另一个。
    static const int kIndex[64] = {
        0,  1,  2, 53,  3,  7, 54, 27,  4, 38, 41,  8, 34, 55, 48, 28,
        62, 5, 39, 46, 44, 42, 22,  9, 24, 35, 59, 56, 49, 18, 29, 11,
        63, 52, 6, 26, 37, 40, 33, 47, 61, 45, 43, 21, 23, 58, 17, 10,
        51, 25, 36, 32, 60, 20, 57, 16, 50, 31, 19, 15, 30, 14, 13, 12,
    };

    const std::uint64_t lowest_bit = value & (~value + 1);  // 只保留最低的那个 1
    const std::uint64_t scrambled = (lowest_bit * 0x022FDD63CC95386DULL) >> 58;
    return kIndex[static_cast<std::size_t>(scrambled) & 63U];
}

}  // namespace

SimHasher::SimHasher(int fingerprint_bits) : bit_count_(fingerprint_bits) {
    // 构造期就拦掉非法参数，保证对象一旦构造成功就一定可用。
    if (fingerprint_bits < 1 || fingerprint_bits > kDefaultFingerprintBits) {
        throw std::invalid_argument("指纹位数必须在 1 ~ 64 之间，实际收到 " +
                                    std::to_string(fingerprint_bits));
    }
}

Fingerprint SimHasher::Compute(const TokenList& tokens) const {
    TermFrequency frequencies;
    frequencies.reserve(tokens.size() / 2 + 1);
    for (const Token& token : tokens) {
        ++frequencies[token];
    }
    return Compute(frequencies);
}

Fingerprint SimHasher::Compute(const TermFrequency& frequencies) const {
    if (frequencies.empty()) {
        throw EmptyDocumentError("文档中没有任何有效词元，无法计算指纹");
    }

    // 累加器用 uint64：权重上限是整篇词元总数，超大文本下 int32 会溢出成负数。
    //
    // 【等价变形：为什么只累加「置位」的比特就够了】
    //   原来的写法是逐位累加：第 i 位为 1 就 + 权重，为 0 就 - 权重，最后看符号。
    //   设 S[i] = 哈希第 i 位为 1 的所有词元的权重之和，W = 全部权重之和，那么
    //       原累加值 = S[i] - (W - S[i]) = 2·S[i] - W
    //   而「> 0」等价于 2·S[i] > W，即 S[i] > W/2（整数除法下也成立）。
    //   所以完全不需要处理为 0 的那些位，只遍历哈希里置位的比特即可 ——
    //   平均 32 次而不是 64 次，而且循环体内没有分支。
    std::vector<std::uint64_t> set_bit_weight(static_cast<std::size_t>(bit_count_), 0);
    std::uint64_t total_weight = 0;

    for (const auto& entry : frequencies) {
        const std::uint64_t weight = static_cast<std::uint64_t>(entry.second);
        total_weight += weight;

        std::uint64_t hash = Fnv1a64(entry.first);
        // h &= h - 1 每次消掉最低位的那个 1，配合 ctz 就能逐个访问置位的比特。
        while (hash != 0) {
            const int bit = CountTrailingZeros64(hash);
            if (bit < bit_count_) {
                set_bit_weight[static_cast<std::size_t>(bit)] += weight;
            }
            hash &= hash - 1;
        }
    }

    const std::uint64_t threshold = total_weight / 2;
    Fingerprint fingerprint = 0;
    for (int bit = 0; bit < bit_count_; ++bit) {
        if (set_bit_weight[static_cast<std::size_t>(bit)] > threshold) {
            fingerprint |= (1ULL << bit);
        }
    }
    return fingerprint;
}

int HammingDistance(Fingerprint lhs, Fingerprint rhs) noexcept {
    return PopCount64(lhs ^ rhs);
}

double DistanceToSimilarity(int hamming_distance, int fingerprint_bits) noexcept {
    if (fingerprint_bits <= 0) {
        return 0.0;
    }

    // 越界时钳制到合法区间，避免调用方传错参数就返回 1.2 或 -0.3。
    int distance = hamming_distance;
    if (distance < 0) {
        distance = 0;
    }
    if (distance > fingerprint_bits) {
        distance = fingerprint_bits;
    }

    const double raw = 1.0 - static_cast<double>(distance) / static_cast<double>(fingerprint_bits);
    return std::round(raw * 100.0) / 100.0;
}

}  // namespace paper_check
