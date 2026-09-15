// SimHash 指纹计算与重复率换算。纯计算、无副作用、不碰文件。
#pragma once

#include "Common.h"

namespace paper_check {

class SimHasher {
public:
    explicit SimHasher(int fingerprint_bits = kDefaultFingerprintBits);

    // 便捷重载：先统计词频再转调下面那个。便于测试和对照，代价是要先物化一遍词频表。
    Fingerprint Compute(const TokenList& tokens) const;

    // 主实现：词频表 -> 指纹。词频为空时抛 EmptyDocumentError。
    Fingerprint Compute(const TermFrequency& frequencies) const;

    int BitCount() const noexcept { return bit_count_; }

private:
    int bit_count_;
};

// 两个指纹不同的二进制位数（异或后数 1），取值 0 ~ 位数。
int HammingDistance(Fingerprint lhs, Fingerprint rhs) noexcept;

// 海明距离换算成重复率，并四舍五入到两位小数，让返回值本身就满足题目的精度要求。
double DistanceToSimilarity(int hamming_distance, int fingerprint_bits) noexcept;

}  // namespace paper_check
