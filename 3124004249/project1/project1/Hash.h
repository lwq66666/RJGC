// FNV-1a 64 位哈希。单独拆成模块，是为了能单独测「哈希是否可复现」。
#pragma once

#include "Common.h"

namespace paper_check {

// 不能用 std::hash<std::string>：标准未规定其算法，换编译器、换版本、甚至加了随机盐之后，
// 同一输入会得到不同结果，导致同一个文件两次运行算出不同的重复率。
std::uint64_t Fnv1a64(const ByteString& key) noexcept;

}  // namespace paper_check
