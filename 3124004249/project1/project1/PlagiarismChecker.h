// 业务编排：把「解码 -> 分词 -> 指纹 -> 重复率」串起来，本身不做具体计算。
#pragma once

#include "Common.h"
#include "SimHasher.h"
#include "Tokenizer.h"

namespace paper_check {

// 一次查重的完整结果。带上中间量，是为了单元测试能直接断言「错在分词还是错在换算」。
struct SimilarityResult {
    double similarity = 0.0;
    int hamming_distance = 0;
    bool both_documents_valid = false;
};

class PlagiarismChecker {
public:
    PlagiarismChecker();

    // 依赖注入，便于测试里换成单字口径或不同的指纹位数。
    PlagiarismChecker(Tokenizer tokenizer, SimHasher hasher);

    // 核心接口：输入两段字节流，输出结果。全程不碰文件系统，所以单元测试可以直接调它。
    // 任一方取不出词元时不抛异常，返回 0.00 且 both_documents_valid = false。
    SimilarityResult CompareBytes(const ByteString& origin_bytes,
                                  const ByteString& copy_bytes) const;

    // 用 RAII 的 TextFile 读两个文件再比较，文件打不开会抛 InputFileError。
    SimilarityResult CompareFiles(const std::string& origin_path,
                                  const std::string& copy_path) const;

    const Tokenizer& GetTokenizer() const noexcept { return tokenizer_; }
    const SimHasher& GetSimHasher() const noexcept { return hasher_; }

private:
    Tokenizer tokenizer_;
    SimHasher hasher_;
};

}  // namespace paper_check
