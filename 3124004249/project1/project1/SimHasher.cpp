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
    value = value - ((value >> 1) & 0x5555555555555555ULL);                 // 每 2 位一组
    value = (value & 0x3333333333333333ULL) + ((value >> 2) & 0x3333333333333333ULL);
    value = (value + (value >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return static_cast<int>((value * 0x0101010101010101ULL) >> 56);
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
    std::unordered_map<ByteString, std::int64_t> term_frequency;
    term_frequency.reserve(tokens.size() / 2 + 1);

    for (const Token& token : tokens) {
        ++term_frequency[token];
    }

    if (term_frequency.empty()) {
        throw EmptyDocumentError("文档中没有任何有效词元，无法计算指纹");
    }

    // 累加器用 int64：权重上限是整篇词元总数，超大文本下 int32 会溢出成负数。
    std::vector<std::int64_t> accumulator(static_cast<std::size_t>(bit_count_), 0);

    for (const auto& entry : term_frequency) {
        const std::uint64_t hash = Fnv1a64(entry.first);
        const std::int64_t weight = entry.second;

        for (int bit = 0; bit < bit_count_; ++bit) {
            const std::size_t index = static_cast<std::size_t>(bit);
            if (((hash >> bit) & 1ULL) != 0ULL) {
                accumulator[index] += weight;
            } else {
                accumulator[index] -= weight;
            }
        }
    }

    Fingerprint fingerprint = 0;
    for (int bit = 0; bit < bit_count_; ++bit) {
        if (accumulator[static_cast<std::size_t>(bit)] > 0) {
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
