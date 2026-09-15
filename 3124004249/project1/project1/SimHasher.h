// SimHash 指纹计算与重复率换算。纯计算、无副作用、不碰文件。
#pragma once

#include "Common.h"

namespace paper_check {

class SimHasher {
public:
    explicit SimHasher(int fingerprint_bits = kDefaultFingerprintBits);

    // 词元列表 -> 指纹：先统计词频作为权重，再按每个哈希位的 1/0 加减权重，
    // 最后按累加值的符号决定该位取值。词元为空时抛 EmptyDocumentError。
    Fingerprint Compute(const TokenList& tokens) const;

    int BitCount() const noexcept { return bit_count_; }

private:
    int bit_count_;
};

// 两个指纹不同的二进制位数（异或后数 1），取值 0 ~ 位数。
int HammingDistance(Fingerprint lhs, Fingerprint rhs) noexcept;

// 海明距离换算成重复率，并四舍五入到两位小数，让返回值本身就满足题目的精度要求。
double DistanceToSimilarity(int hamming_distance, int fingerprint_bits) noexcept;

}  // namespace paper_check
