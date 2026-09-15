#include "PlagiarismChecker.h"

#include <utility>

#include "Exceptions.h"
#include "TextFile.h"
#include "UnicodeText.h"

namespace paper_check {
namespace {

// 计算单个文档的指纹。返回 false 表示取不出词元。
// 在这一层消化 EmptyDocumentError，是为了把「拒绝计算之后怎么办」的决定收在一处，
// 而不是散落到每个调用点。
bool TryComputeFingerprint(const Tokenizer& tokenizer, const SimHasher& hasher,
                           const ByteString& bytes, Fingerprint& fingerprint_out) {
    const CodePointList code_points = unicode::Decode(bytes);
    // 走 CountTokens 而不是 Split：边分词边计数，省掉一个上百万元素的中间数组。
    const TermFrequency frequencies = tokenizer.CountTokens(code_points);

    try {
        fingerprint_out = hasher.Compute(frequencies);
        return true;
    } catch (const EmptyDocumentError&) {
        fingerprint_out = 0;
        return false;
    }
}

}  // namespace

PlagiarismChecker::PlagiarismChecker() : tokenizer_(), hasher_() {}

PlagiarismChecker::PlagiarismChecker(Tokenizer tokenizer, SimHasher hasher)
    : tokenizer_(std::move(tokenizer)), hasher_(std::move(hasher)) {}

SimilarityResult PlagiarismChecker::CompareBytes(const ByteString& origin_bytes,
                                                 const ByteString& copy_bytes) const {
    SimilarityResult result;

    Fingerprint origin_fingerprint = 0;
    Fingerprint copy_fingerprint = 0;

    const bool origin_valid =
        TryComputeFingerprint(tokenizer_, hasher_, origin_bytes, origin_fingerprint);
    const bool copy_valid =
        TryComputeFingerprint(tokenizer_, hasher_, copy_bytes, copy_fingerprint);

    result.both_documents_valid = origin_valid && copy_valid;
    if (!result.both_documents_valid) {
        // 一方没内容可取时「相似度」无从谈起。这里明确给 0.00，
        // 而不是让两个空指纹相比得出 1.00 那种假结论。
        result.similarity = 0.0;
        result.hamming_distance = 0;
        return result;
    }

    result.hamming_distance = HammingDistance(origin_fingerprint, copy_fingerprint);
    result.similarity = DistanceToSimilarity(result.hamming_distance, hasher_.BitCount());
    return result;
}

SimilarityResult PlagiarismChecker::CompareFiles(const std::string& origin_path,
                                                 const std::string& copy_path) const {
    // 两个 TextFile 都是栈上 RAII 对象：copy_path 打不开时构造函数抛异常，
    // 已在栈上的 origin_file 会被正常析构，不会泄漏句柄。
    TextFile origin_file(origin_path);
    TextFile copy_file(copy_path);
    return CompareBytes(origin_file.ReadAll(), copy_file.ReadAll());
}

}  // namespace paper_check
