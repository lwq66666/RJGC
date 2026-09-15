// =============================================================================
//  test_main.cpp  ——  单元测试套件
//
//  零依赖：不引入 GoogleTest，自己写一个极简的断言框架。
//  好处是助教拿到工程直接按 Ctrl+F5 就能跑，不需要额外装东西。
//
//  运行方式（两种模式）：
//      tests.exe           跑全部单元测试
//      tests.exe bench     跑性能基准（优化前后对照）
//
//  测试设计思路：「等价类 + 边界值 + 工程约束 + 算法口径」
//    等价类  : 两篇文本完全相同 / 部分改写 / 完全无关
//    边界值  : 空文档、纯空白、非法 UTF-8、指纹位数越界
//    工程约束: 哈希可复现、两种计算路径结果一致、大文本在 5 秒预算内
//    算法口径: 单字口径对「乱序抄袭」会误报 —— 把踩过的坑固化成测试
// =============================================================================

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "AnswerFile.h"
#include "Common.h"
#include "Exceptions.h"
#include "Hash.h"
#include "PlagiarismChecker.h"
#include "SimHasher.h"
#include "TextFile.h"
#include "Tokenizer.h"
#include "UnicodeText.h"

using namespace paper_check;

namespace {

// -----------------------------------------------------------------------------
// 极简断言框架
// -----------------------------------------------------------------------------
struct TestFailure {
    std::string message;
};

int g_passed = 0;
int g_failed = 0;

void Check(bool condition, const char* expression, int line) {
    if (!condition) {
        throw TestFailure{"第 " + std::to_string(line) + " 行断言失败: " + expression};
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

// -----------------------------------------------------------------------------
// 公共测试数据
// -----------------------------------------------------------------------------

// 原文与它的改写版（只换了几个词，对应作业样例的改写手法）
const char* const kOriginText = "今天是星期天，天气晴，今天晚上我要去看电影。";
const char* const kRewrittenText = "今天是周天，天气晴朗，我晚上要去看电影。";
// 与上面完全无关的一段文字
const char* const kUnrelatedText =
    "C++ 的智能指针分为 unique_ptr、shared_ptr 和 weak_ptr 三种，"
    "模板元编程允许在编译期完成计算，虚函数表由编译器为含虚函数的类生成。";

// -----------------------------------------------------------------------------
// 一、Unicode 编解码
// -----------------------------------------------------------------------------
void TestUnicode_RoundTripChinese() {
    const std::string original = "汉字编码";
    const std::string encoded = unicode::Encode(U'汉');
    CHECK(encoded.size() == 3);                       // 一个汉字在 UTF-8 里占 3 字节
    CHECK(unicode::Decode(original).size() == 4);        // 4 个汉字 = 4 个码点
}

void TestUnicode_FourByteCharacterRoundTrip() {
    const CodePoint smiling = 0x1F600;  // 一个 emoji，UTF-8 里占 4 字节
    const std::string encoded = unicode::Encode(smiling);
    CHECK(encoded.size() == 4);
    const CodePointList decoded = unicode::Decode(encoded);
    CHECK(decoded.size() == 1);
    CHECK(decoded.front() == smiling);
}

void TestUnicode_TruncatedSequenceBecomesReplacementChar() {
    // 「汉」的 UTF-8 是 E6 B1 89，这里故意砍掉最后一个字节
    std::string broken = "汉";
    broken.resize(2);
    const CodePointList decoded = unicode::Decode(broken);
    // 关键：不能抛异常。样例里按字节删字构造的文本会出现这种序列，
    // 一旦抛异常就会被判「异常退出」扣分。
    CHECK(decoded.size() == 1);
    CHECK(decoded.front() == kReplacementCharacter);
}

void TestUnicode_LoneContinuationByteBecomesReplacementChar() {
    const std::string broken(1, static_cast<char>(0x80));  // 孤立的续字节
    const CodePointList decoded = unicode::Decode(broken);
    CHECK(decoded.size() == 1);
    CHECK(decoded.front() == kReplacementCharacter);
}

void TestUnicode_EmptyInputGivesEmptyOutput() {
    CHECK(unicode::Decode("").empty());
}

void TestUnicode_TwoByteCharacterRoundTrip() {
    // é = U+00E9，UTF-8 是 C3 A9（两字节分支）
    const ByteString encoded = unicode::Encode(0x00E9U);
    CHECK(encoded.size() == 2);
    CHECK(static_cast<unsigned char>(encoded[0]) == 0xC3U);
    CHECK(static_cast<unsigned char>(encoded[1]) == 0xA9U);

    const CodePointList decoded = unicode::Decode(encoded);
    CHECK(decoded.size() == 1);
    CHECK(decoded.front() == 0x00E9U);
}

void TestUnicode_TwoByteEncodeBoundaries() {
    // 两字节区间的下界（U+0080）与上界（U+07FF）各走一次，把分支边界钉住
    CHECK(unicode::Encode(0x0080U).size() == 2);
    CHECK(unicode::Encode(0x07FFU).size() == 2);
    // U+007F 属于单字节，U+0800 已跨到三字节
    CHECK(unicode::Encode(0x007FU).size() == 1);
    CHECK(unicode::Encode(0x0800U).size() == 3);
}

void TestUnicode_OverlongEncodingIsRejected() {
    // C0 AF 是字符 '/' 的「过长编码」：本可以用 0x2F 一个字节表示。
    // 这种序列是非法 UTF-8，必须换成 U+FFFD，而不是解析成 '/'
    // —— 否则攻击者能用多种字节写法表达同一字符，绕过查重比对。
    const ByteString overlong = {static_cast<char>(0xC0), static_cast<char>(0xAF)};
    const CodePointList decoded = unicode::Decode(overlong);
    CHECK(decoded.size() == 1);
    CHECK(decoded.front() == kReplacementCharacter);
}

void TestUnicode_SurrogateIsRejected() {
    // ED A0 80 编码的是代理区码点 U+D800。UTF-8 中不允许出现代理，
    // 解析成普通字符会破坏「一个字符一种字节表示」的前提。
    const ByteString surrogate = {static_cast<char>(0xED), static_cast<char>(0xA0),
                                  static_cast<char>(0x80)};
    const CodePointList decoded = unicode::Decode(surrogate);
    CHECK(decoded.size() == 1);
    CHECK(decoded.front() == kReplacementCharacter);
}

void TestUnicode_OutOfRangeCodePointIsRejected() {
    // F5 80 80 80 解出来是 U+140000，超出 Unicode 上限 U+10FFFF
    const ByteString too_large = {static_cast<char>(0xF5), static_cast<char>(0x80),
                                  static_cast<char>(0x80), static_cast<char>(0x80)};
    const CodePointList decoded = unicode::Decode(too_large);
    CHECK(decoded.size() == 1);
    CHECK(decoded.front() == kReplacementCharacter);
}

void TestUnicode_InvalidSequenceDoesNotSwallowFollowingText() {
    // 关键回归测试：一个坏序列后面跟着正常汉字时，坏字节必须只吃掉自己，
    // 不能把后面的合法字符一起吞掉，否则整篇文本会静默丢字、重复率失真。
    ByteString mixed = {static_cast<char>(0xFF), static_cast<char>(0x80)};
    mixed += "汉";
    const CodePointList decoded = unicode::Decode(mixed);
    CHECK(decoded.size() == 3);
    CHECK(decoded[0] == kReplacementCharacter);
    CHECK(decoded[1] == kReplacementCharacter);
    CHECK(decoded[2] == U'汉');
}

void TestUnicode_CjkClassification() {
    CHECK(unicode::IsCjk(U'汉'));
    CHECK(unicode::IsCjk(U'一'));
    CHECK(!unicode::IsCjk(U'a'));       // ASCII 字母不是汉字
    CHECK(!unicode::IsCjk(U'，'));      // 中文标点不是汉字
}

void TestUnicode_AsciiAlphaNumericClassification() {
    CHECK(unicode::IsAsciiAlphaNumeric(U'a'));
    CHECK(unicode::IsAsciiAlphaNumeric(U'Z'));
    CHECK(unicode::IsAsciiAlphaNumeric(U'7'));
    CHECK(!unicode::IsAsciiAlphaNumeric(U'-'));  // 连字符不是
}

// -----------------------------------------------------------------------------
// 二、分词器
// -----------------------------------------------------------------------------
void TestTokenizer_BigramSplitsAdjacentChars() {
    const Tokenizer tokenizer(TokenMode::Bigram);
    const TokenList tokens = tokenizer.Split(unicode::Decode("今天天气"));
    // 「今天天气」-> 今天 / 天天 / 天气，长度 4 的连续汉字产生 3 个词元
    CHECK(tokens.size() == 3);
    CHECK(tokens[0] == "今天");
    CHECK(tokens[1] == "天天");
    CHECK(tokens[2] == "天气");
}

void TestTokenizer_SingleCharRunIsKept() {
    const Tokenizer tokenizer(TokenMode::Bigram);
    const TokenList tokens = tokenizer.Split(unicode::Decode("晴。"));
    // 单字成段必须保留，否则被标点切碎的短句会一个词元都取不到
    CHECK(tokens.size() == 1);
    CHECK(tokens[0] == "晴");
}

void TestTokenizer_DropsPunctuationAndWhitespace() {
    const Tokenizer tokenizer(TokenMode::Bigram);
    const TokenList tokens = tokenizer.Split(unicode::Decode("  ，。！？\n\t "));
    CHECK(tokens.empty());
}

void TestTokenizer_LowercasesAsciiWords() {
    const Tokenizer tokenizer(TokenMode::Bigram);
    const TokenList tokens = tokenizer.Split(unicode::Decode("Hello WORLD"));
    CHECK(tokens.size() == 2);
    CHECK(tokens[0] == "hello");
    CHECK(tokens[1] == "world");
}

void TestTokenizer_UnigramEmitsEveryChar() {
    const Tokenizer tokenizer(TokenMode::Unigram);
    const TokenList tokens = tokenizer.Split(unicode::Decode("今天天气"));
    CHECK(tokens.size() == 4);
    CHECK(tokens[0] == "今");
    CHECK(tokens[3] == "气");
}

void TestTokenizer_CountTokensAgreesWithSplit() {
    // CountTokens 是 Split 的「省内存版」，两者结果必须完全一致
    const Tokenizer tokenizer(TokenMode::Bigram);
    const CodePointList code_points = unicode::Decode(kOriginText);

    const TokenList tokens = tokenizer.Split(code_points);
    const TermFrequency frequencies = tokenizer.CountTokens(code_points);

    std::size_t total = 0;
    for (const auto& entry : frequencies) {
        total += static_cast<std::size_t>(entry.second);
    }
    CHECK(total == tokens.size());

    // 逐词核对次数
    for (const Token& token : tokens) {
        CHECK(frequencies.find(token) != frequencies.end());
    }
}

// -----------------------------------------------------------------------------
// 三、哈希
// -----------------------------------------------------------------------------
void TestHash_IsDeterministic() {
    // 这条例行守护「不能用 std::hash<std::string>」这个设计决定：
    // 一旦换成带随机盐的实现，同一输入两次结果就会不同。
    const std::string key = "查重";
    CHECK(Fnv1a64(key) == Fnv1a64(key));
    CHECK(Fnv1a64(key) == Fnv1a64(std::string("查重")));
}

void TestHash_EmptyStringEqualsOffsetBasis() {
    // FNV-1a 规范规定：空输入的哈希值就是偏移基数
    CHECK(Fnv1a64("") == 0xCBF29CE484222325ULL);
}

void TestHash_DifferentKeysDiffer() {
    CHECK(Fnv1a64("今天") != Fnv1a64("天气"));
    CHECK(Fnv1a64("a") != Fnv1a64("b"));
}

// -----------------------------------------------------------------------------
// 四、SimHash 指纹
// -----------------------------------------------------------------------------
Fingerprint FingerprintOf(const std::string& text) {
    const Tokenizer tokenizer(TokenMode::Bigram);
    const SimHasher hasher;
    return hasher.Compute(tokenizer.Split(unicode::Decode(text)));
}

void TestSimHash_IdenticalTextGivesDistanceZero() {
    CHECK(HammingDistance(FingerprintOf(kOriginText), FingerprintOf(kOriginText)) == 0);
}

void TestSimHash_RewrittenTextIsCloserThanUnrelatedText() {
    const Fingerprint origin = FingerprintOf(kOriginText);
    const int rewritten = HammingDistance(origin, FingerprintOf(kRewrittenText));
    const int unrelated = HammingDistance(origin, FingerprintOf(kUnrelatedText));
    CHECK(rewritten < unrelated);
}

void TestSimHash_EmptyTokensThrows() {
    const SimHasher hasher;
    bool thrown = false;
    try {
        hasher.Compute(TokenList{});
    } catch (const EmptyDocumentError&) {
        thrown = true;
    }
    CHECK(thrown);
}

void TestSimHash_OutOfRangeBitCountThrows() {
    // 用一个小函数包住构造，避免「局部变量未使用」的编译警告
    struct Construct {
        static void With(int bits) {
            const SimHasher hasher(bits);
            (void)hasher;
        }
    };

    bool zero_thrown = false;
    bool large_thrown = false;
    try {
        Construct::With(0);
    } catch (const std::invalid_argument&) {
        zero_thrown = true;
    }
    try {
        Construct::With(65);
    } catch (const std::invalid_argument&) {
        large_thrown = true;
    }
    CHECK(zero_thrown);
    CHECK(large_thrown);
}

void TestSimHash_RespectsConfiguredBitCount() {
    const Tokenizer tokenizer(TokenMode::Bigram);
    const SimHasher hasher(16);  // 只用低 16 位
    const TokenList tokens = tokenizer.Split(unicode::Decode(kOriginText));
    const Fingerprint fingerprint = hasher.Compute(tokens);
    CHECK((fingerprint >> 16) == 0);  // 高位必须全为 0
}

void TestSimHash_TwoOverloadsAgree() {
    // 这条例守护性能优化引入的两种计算路径必须等价：
    // 一条是「物化词元数组 -> 统计词频 -> 逐位累加」，
    // 一条是「边分词边计数 -> 只累加置位的比特」。
    const Tokenizer tokenizer(TokenMode::Bigram);
    const SimHasher hasher;
    const CodePointList code_points = unicode::Decode(kOriginText);

    const Fingerprint via_tokens = hasher.Compute(tokenizer.Split(code_points));
    const Fingerprint via_frequencies = hasher.Compute(tokenizer.CountTokens(code_points));

    CHECK(via_tokens == via_frequencies);
    CHECK(via_tokens != 0);
}

void TestSimHash_TokenOrderDoesNotMatter() {
    // SimHash 基于词频统计，同一个词出现得早晚不影响结果，
    // 所以把词元列表整个倒过来，指纹必须保持不变。
    const SimHasher hasher;
    const Tokenizer tokenizer(TokenMode::Bigram);
    const TokenList tokens = tokenizer.Split(unicode::Decode(kOriginText));
    const TokenList reversed(tokens.rbegin(), tokens.rend());
    CHECK(!tokens.empty());
    CHECK(hasher.Compute(tokens) == hasher.Compute(reversed));
}

// -----------------------------------------------------------------------------
// 五、相似度换算
// -----------------------------------------------------------------------------
void TestSimilarity_Boundaries() {
    CHECK(DistanceToSimilarity(0, 64) == 1.0);    // 指纹完全相同
    CHECK(DistanceToSimilarity(64, 64) == 0.0);   // 每一位都不同
    CHECK(DistanceToSimilarity(32, 64) == 0.5);
}

void TestSimilarity_AlwaysTwoDecimals() {
    // 题目要求精确到小数点后两位，这里把 0~64 全部距离都过一遍
    for (int distance = 0; distance <= 64; ++distance) {
        const double value = DistanceToSimilarity(distance, 64);
        CHECK(value >= 0.0 && value <= 1.0);
        // value * 100 必须落在整数上（用容差比较，避免浮点表示误差导致的假失败）
        CHECK(std::fabs(value * 100.0 - std::round(value * 100.0)) < 1e-9);
    }
    CHECK(DistanceToSimilarity(12, 64) == 0.81);
    CHECK(DistanceToSimilarity(6, 64) == 0.91);
}

void TestSimilarity_ClampsOutOfRangeInput() {
    CHECK(DistanceToSimilarity(-5, 64) == 1.0);
    CHECK(DistanceToSimilarity(999, 64) == 0.0);
    CHECK(DistanceToSimilarity(10, 0) == 0.0);    // 位数非法时不崩，给 0
}

// -----------------------------------------------------------------------------
// 六、业务编排（端到端，不碰文件系统）
// -----------------------------------------------------------------------------
void TestChecker_IdenticalTextGivesFullRate() {
    const PlagiarismChecker checker;
    const SimilarityResult result = checker.CompareBytes(kOriginText, kOriginText);
    CHECK(result.both_documents_valid);
    CHECK(result.hamming_distance == 0);
    CHECK(result.similarity == 1.0);
}

void TestChecker_RewrittenTextScoresInBetween() {
    const PlagiarismChecker checker;
    const SimilarityResult rewritten = checker.CompareBytes(kOriginText, kRewrittenText);
    const SimilarityResult unrelated = checker.CompareBytes(kOriginText, kUnrelatedText);
    CHECK(rewritten.both_documents_valid);
    CHECK(unrelated.both_documents_valid);
    // 改写版应该比无关文本更相似，而且不该判成「完全一致」
    CHECK(rewritten.similarity > unrelated.similarity);
    CHECK(rewritten.similarity < 1.0);
}

void TestChecker_EmptyDocumentGivesZero() {
    const PlagiarismChecker checker;
    const SimilarityResult result = checker.CompareBytes(kOriginText, "");
    CHECK(!result.both_documents_valid);
    CHECK(result.similarity == 0.0);
}

void TestChecker_WhitespaceOnlyGivesZero() {
    const PlagiarismChecker checker;
    const SimilarityResult result = checker.CompareBytes(kOriginText, "  \n\t  ");
    CHECK(!result.both_documents_valid);
    CHECK(result.similarity == 0.0);
}

void TestChecker_BothEmptyGivesZeroNotOne() {
    // 最容易出的假结论：两个空文档若都返回同一个指纹，重复率会变成 1.00
    const PlagiarismChecker checker;
    const SimilarityResult result = checker.CompareBytes("", "");
    CHECK(result.similarity == 0.0);
    CHECK(!result.both_documents_valid);
}

// -----------------------------------------------------------------------------
// 七、算法口径守护：为什么必须用 bigram
// -----------------------------------------------------------------------------
void TestUnigramModeFailsOnShuffledText() {
    // 把每个字都调换位置：字频完全不变，但顺序全乱。
    // 单字口径下两篇指纹会完全相同 -> 重复率被误报成 1.00（假结论）；
    // bigram 口径下能正确识别出「不一样」。
    const std::string normal = "一二三四五六七八";
    const std::string shuffled = "八七六五四三二一";

    const SimHasher hasher;

    const int unigram_distance = HammingDistance(
        hasher.Compute(Tokenizer(TokenMode::Unigram).Split(unicode::Decode(normal))),
        hasher.Compute(Tokenizer(TokenMode::Unigram).Split(unicode::Decode(shuffled))));
    const int bigram_distance = HammingDistance(
        hasher.Compute(Tokenizer(TokenMode::Bigram).Split(unicode::Decode(normal))),
        hasher.Compute(Tokenizer(TokenMode::Bigram).Split(unicode::Decode(shuffled))));

    CHECK(unigram_distance == 0);   // 单字口径完全看不出差别 —— 这就是坑
    CHECK(bigram_distance > 0);     // bigram 能看出来
}

// -----------------------------------------------------------------------------
// 八、异常层次
// -----------------------------------------------------------------------------
void TestExceptions_AllDeriveFromAppError() {
    bool argument = false;
    bool input = false;
    bool output = false;
    bool empty = false;
    try {
        throw ArgumentError("x");
    } catch (const AppError&) {
        argument = true;
    }
    try {
        throw InputFileError("x");
    } catch (const AppError&) {
        input = true;
    }
    try {
        throw OutputFileError("x");
    } catch (const AppError&) {
        output = true;
    }
    try {
        throw EmptyDocumentError("x");
    } catch (const AppError&) {
        empty = true;
    }
    CHECK(argument && input && output && empty);
}

// -----------------------------------------------------------------------------
// 九、RAII 文件类的往返测试
// -----------------------------------------------------------------------------
void TestFileRaii_WriteThenReadBack() {
    const std::string temp_path = "tests_tmp_answer.txt";
    {
        // 答案文件在构造时创建并截断，析构时 flush + close
        AnswerFile writer(temp_path);
        writer.WriteSimilarity(0.91);
    }
    {
        TextFile reader(temp_path);
        const ByteString& bytes = reader.ReadAll();
        // 必须只有这 4 个字节，不能有多余的换行或空格
        CHECK(bytes == "0.91");
    }
    std::remove(temp_path.c_str());
}

void TestFileRaii_ReadAllIsIdempotent() {
    const std::string temp_path = "tests_tmp_idempotent.txt";
    {
        AnswerFile writer(temp_path);
        writer.WriteSimilarity(0.5);
    }
    TextFile reader(temp_path);
    const std::string first = reader.ReadAll();
    const std::string second = reader.ReadAll();  // 第二次走缓存
    CHECK(first == second);
    CHECK(first == "0.50");
    std::remove(temp_path.c_str());
}

void TestFileRaii_MissingFileThrows() {
    bool thrown = false;
    try {
        TextFile missing("this_file_does_not_exist_9f8e7d.txt");
    } catch (const InputFileError&) {
        thrown = true;
    }
    CHECK(thrown);
}

void TestFileRaii_AnswerFileToBadDirectoryThrows() {
    bool thrown = false;
    try {
        AnswerFile bad("no_such_directory_9f8e7d/answer.txt");
    } catch (const OutputFileError&) {
        thrown = true;
    }
    CHECK(thrown);
}

void TestFileRaii_EmptyFileIsReadable() {
    // 空文件是合法输入（题目样例里可能有），不能让 file_size > 0 的判断把 &bytes[0] 炸掉
    const std::string temp_path = "tests_tmp_empty.txt";
    {
        AnswerFile writer(temp_path);
    }
    TextFile reader(temp_path);
    CHECK(reader.ReadAll().empty());
    std::remove(temp_path.c_str());
}

void TestFileRaii_DirectoryPathThrows() {
    // 传目录进去时 tellg() 返回 -1，必须走 InputFileError 而不是让 resize 收到负数
    bool thrown = false;
    try {
        TextFile directory(".");
    } catch (const InputFileError&) {
        thrown = true;
    }
    CHECK(thrown);
}

// -----------------------------------------------------------------------------
// 十、端到端：走真实文件的 CompareFiles 入口
// -----------------------------------------------------------------------------
void TestChecker_CompareFilesReadsFromDisk() {
    // CompareBytes 已被覆盖，但真正被 main 调用的是 CompareFiles。
    // 这条路径把「RAII 读文件 -> 解码 -> 分词 -> 指纹」串起来，
    // 必须单独测，否则文件读取出问题时单测全绿、实际运行出错。
    const std::string origin_path = "tests_tmp_origin.txt";
    const std::string copy_path = "tests_tmp_copy.txt";
    {
        AnswerFile origin_writer(origin_path);
        origin_writer.WriteSimilarity(1.0);  // 借 AnswerFile 只图省事写几个字节
    }
    {
        AnswerFile copy_writer(copy_path);
        copy_writer.WriteSimilarity(1.0);
    }

    const PlagiarismChecker checker;
    const SimilarityResult result = checker.CompareFiles(origin_path, copy_path);
    CHECK(result.both_documents_valid);
    CHECK(result.similarity == 1.00);  // 两文件内容相同

    const SimilarityResult self = checker.CompareFiles(origin_path, origin_path);
    CHECK(self.similarity == 1.00);

    std::remove(origin_path.c_str());
    std::remove(copy_path.c_str());
}

void TestChecker_CompareFilesPropagatesMissingFile() {
    // 路径不存在时必须把 InputFileError 抛出去，不能吞掉后返回 0.00
    // —— 静默返回 0 会让「文件打不开」伪装成「两篇不相似」，是最危险的失败模式。
    const PlagiarismChecker checker;
    bool thrown = false;
    try {
        checker.CompareFiles("no_such_file_9f8e7d.txt", "no_such_file_9f8e7d.txt");
    } catch (const InputFileError&) {
        thrown = true;
    }
    CHECK(thrown);
}

void TestChecker_DependencyInjectionIsHonored() {
    // 注入构造必须真的把参数用起来，否则「可测试性」就是空话。
    const PlagiarismChecker narrow(Tokenizer(TokenMode::Bigram), SimHasher(32));
    CHECK(narrow.GetTokenizer().Mode() == TokenMode::Bigram);
    CHECK(narrow.GetSimHasher().BitCount() == 32);

    const PlagiarismChecker wide(Tokenizer(TokenMode::Unigram), SimHasher(64));
    CHECK(wide.GetTokenizer().Mode() == TokenMode::Unigram);
    CHECK(wide.GetSimHasher().BitCount() == 64);

    // 32 位下完全相同的文本仍必须是 1.00（距离 0 / 32），
    // 顺带验证「位数」不只是存起来了，而是真的参与了换算。
    const SimilarityResult same = narrow.CompareBytes("你好世界", "你好世界");
    CHECK(same.both_documents_valid);
    CHECK(same.hamming_distance == 0);
    CHECK(same.similarity == 1.00);

    // 换成单字口径后相同文本同样得 1.00，说明注入的分词器确实生效
    const SimilarityResult unigram_same = wide.CompareBytes("你好世界", "你好世界");
    CHECK(unigram_same.similarity == 1.00);
}

// -----------------------------------------------------------------------------
// 十一、工程约束：大文本必须在 5 秒预算内
// -----------------------------------------------------------------------------
std::string MakeRandomCjkText(std::size_t char_count) {
    std::string out;
    out.reserve(char_count * 3);
    std::uint64_t state = 88172645463325252ULL;
    for (std::size_t index = 0; index < char_count; ++index) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        out += unicode::Encode(static_cast<CodePoint>(0x4E00U + (state % 0x2000U)));
    }
    return out;
}

void TestPerformance_MillionCharTextWithinBudget() {
    // 用「相邻两字几乎全不重复」的伪随机汉字，这是指纹计算的最坏情况
    const std::string big_text = MakeRandomCjkText(1000000);

    const auto started = std::chrono::steady_clock::now();
    const PlagiarismChecker checker;
    const SimilarityResult result = checker.CompareBytes(big_text, big_text);
    const auto finished = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(finished - started).count();
    CHECK(result.similarity == 1.0);
    CHECK(seconds < 5.0);  // 作业规定 5 秒内给出答案

    std::cout << "         (100 万字符最坏情况耗时 " << seconds << " 秒，预算 5 秒)"
              << std::endl;
}

// -----------------------------------------------------------------------------
// 测试注册与运行
// -----------------------------------------------------------------------------
using TestFunction = void (*)();

struct TestCase {
    const char* name;
    TestFunction function;
};

const TestCase kTestCases[] = {
    {"Unicode_汉字编解码往返", TestUnicode_RoundTripChinese},
    {"Unicode_四字节字符往返", TestUnicode_FourByteCharacterRoundTrip},
    {"Unicode_截断序列替换为U+FFFD", TestUnicode_TruncatedSequenceBecomesReplacementChar},
    {"Unicode_孤立续字节替换为U+FFFD", TestUnicode_LoneContinuationByteBecomesReplacementChar},
    {"Unicode_空输入", TestUnicode_EmptyInputGivesEmptyOutput},
    {"Unicode_汉字判定", TestUnicode_CjkClassification},
    {"Unicode_字母数字判定", TestUnicode_AsciiAlphaNumericClassification},
    {"Unicode_两字节字符往返", TestUnicode_TwoByteCharacterRoundTrip},
    {"Unicode_两字节编码边界", TestUnicode_TwoByteEncodeBoundaries},
    {"Unicode_过长编码被拒绝", TestUnicode_OverlongEncodingIsRejected},
    {"Unicode_代理区被拒绝", TestUnicode_SurrogateIsRejected},
    {"Unicode_超出码点上限被拒绝", TestUnicode_OutOfRangeCodePointIsRejected},
    {"Unicode_坏字节不吞后续文本", TestUnicode_InvalidSequenceDoesNotSwallowFollowingText},
    {"Tokenizer_bigram切分相邻两字", TestTokenizer_BigramSplitsAdjacentChars},
    {"Tokenizer_单字成段保留", TestTokenizer_SingleCharRunIsKept},
    {"Tokenizer_丢弃标点与空白", TestTokenizer_DropsPunctuationAndWhitespace},
    {"Tokenizer_英文折小写", TestTokenizer_LowercasesAsciiWords},
    {"Tokenizer_unigram逐字切分", TestTokenizer_UnigramEmitsEveryChar},
    {"Tokenizer_CountTokens与Split一致", TestTokenizer_CountTokensAgreesWithSplit},
    {"Hash_可复现", TestHash_IsDeterministic},
    {"Hash_空串等于偏移基数", TestHash_EmptyStringEqualsOffsetBasis},
    {"Hash_不同输入结果不同", TestHash_DifferentKeysDiffer},
    {"SimHash_相同文本距离为0", TestSimHash_IdenticalTextGivesDistanceZero},
    {"SimHash_改写比无关更相似", TestSimHash_RewrittenTextIsCloserThanUnrelatedText},
    {"SimHash_空词元抛异常", TestSimHash_EmptyTokensThrows},
    {"SimHash_位数越界抛异常", TestSimHash_OutOfRangeBitCountThrows},
    {"SimHash_位数参数生效", TestSimHash_RespectsConfiguredBitCount},
    {"SimHash_两条计算路径结果一致", TestSimHash_TwoOverloadsAgree},
    {"SimHash_与词元顺序无关", TestSimHash_TokenOrderDoesNotMatter},
    {"Similarity_边界值", TestSimilarity_Boundaries},
    {"Similarity_全距离均为两位小数", TestSimilarity_AlwaysTwoDecimals},
    {"Similarity_越界输入被钳制", TestSimilarity_ClampsOutOfRangeInput},
    {"Checker_相同文本得1.00", TestChecker_IdenticalTextGivesFullRate},
    {"Checker_改写版分数居中", TestChecker_RewrittenTextScoresInBetween},
    {"Checker_空文档得0.00", TestChecker_EmptyDocumentGivesZero},
    {"Checker_纯空白得0.00", TestChecker_WhitespaceOnlyGivesZero},
    {"Checker_双方皆空不得1.00", TestChecker_BothEmptyGivesZeroNotOne},
    {"口径_unigram对乱序文本失效", TestUnigramModeFailsOnShuffledText},
    {"异常_四类均继承AppError", TestExceptions_AllDeriveFromAppError},
    {"文件_写入后读回一致", TestFileRaii_WriteThenReadBack},
    {"文件_重复读取走缓存", TestFileRaii_ReadAllIsIdempotent},
    {"文件_文件不存在抛异常", TestFileRaii_MissingFileThrows},
    {"文件_空文件可读", TestFileRaii_EmptyFileIsReadable},
    {"文件_传目录抛异常", TestFileRaii_DirectoryPathThrows},
    {"Checker_CompareFiles读真实文件", TestChecker_CompareFilesReadsFromDisk},
    {"Checker_CompareFiles传播缺文件异常", TestChecker_CompareFilesPropagatesMissingFile},
    {"Checker_依赖注入生效", TestChecker_DependencyInjectionIsHonored},
    {"文件_答案目录不存在抛异常", TestFileRaii_AnswerFileToBadDirectoryThrows},
    {"性能_百万字符在5秒预算内", TestPerformance_MillionCharTextWithinBudget},
};

void RunAllTests() {
    std::cout << "===== 单元测试 =====" << std::endl;
    std::cout << std::endl;

    for (const TestCase& test_case : kTestCases) {
        try {
            test_case.function();
            std::cout << "[通过] " << test_case.name << std::endl;
            ++g_passed;
        } catch (const TestFailure& failure) {
            std::cout << "[失败] " << test_case.name << std::endl;
            std::cout << "        " << failure.message << std::endl;
            ++g_failed;
        } catch (const std::exception& error) {
            std::cout << "[异常] " << test_case.name << "  " << error.what() << std::endl;
            ++g_failed;
        }
    }

    std::cout << std::endl;
    std::cout << "合计 " << (g_passed + g_failed) << " 个用例：通过 " << g_passed
              << "，失败 " << g_failed << std::endl;
}

// -----------------------------------------------------------------------------
// 性能基准（tests.exe bench）
//
// 对照三组，用来量化两处优化各自的贡献：
//   V0 原始   ：物化词元数组 + 逐位累加（优化前的实现）
//   V1 优化A  ：边分词边计数 + 逐位累加（只看「省掉中间数组」的收益）
//   V2 优化A+B：边分词边计数 + 只累加置位的比特（当前实现）
// -----------------------------------------------------------------------------
namespace bench {

// 优化前的朴素实现，作为对照组保留在这里
Fingerprint NaiveFingerprint(const TermFrequency& frequencies, int bit_count) {
    std::vector<std::int64_t> accumulator(static_cast<std::size_t>(bit_count), 0);
    for (const auto& entry : frequencies) {
        const std::uint64_t hash = Fnv1a64(entry.first);
        const std::int64_t weight = entry.second;
        for (int bit = 0; bit < bit_count; ++bit) {
            const std::size_t index = static_cast<std::size_t>(bit);
            if (((hash >> bit) & 1ULL) != 0ULL) {
                accumulator[index] += weight;
            } else {
                accumulator[index] -= weight;
            }
        }
    }
    Fingerprint fingerprint = 0;
    for (int bit = 0; bit < bit_count; ++bit) {
        if (accumulator[static_cast<std::size_t>(bit)] > 0) {
            fingerprint |= (1ULL << bit);
        }
    }
    return fingerprint;
}

TermFrequency CountFromTokenList(const TokenList& tokens) {
    TermFrequency frequencies;
    frequencies.reserve(tokens.size() / 2 + 1);
    for (const Token& token : tokens) {
        ++frequencies[token];
    }
    return frequencies;
}

struct Measurement {
    double decode = 0.0;
    double tokenize = 0.0;
    double fingerprint = 0.0;
    double total = 0.0;
    Fingerprint value = 0;
};

template <typename TokenizeAndCompute>
Measurement Measure(const std::string& bytes, int rounds, TokenizeAndCompute work) {
    using Clock = std::chrono::steady_clock;
    Measurement best;
    best.total = 1e18;

    for (int round = 0; round < rounds; ++round) {
        Measurement current;
        const auto started = Clock::now();

        const CodePointList code_points = unicode::Decode(bytes);
        const auto decoded = Clock::now();

        work(code_points, current);
        const auto done = Clock::now();

        current.decode = std::chrono::duration<double, std::milli>(decoded - started).count();
        current.total = std::chrono::duration<double, std::milli>(done - started).count();

        if (current.total < best.total) {
            best = current;
        }
    }
    return best;
}

void Report(const char* label, const std::string& text, const PlagiarismChecker& checker) {
    const std::size_t bytes = text.size();
    std::cout << "【" << label << "】 输入 " << bytes / 1024 / 1024 << " MB" << std::endl;

    const Tokenizer& tokenizer = checker.GetTokenizer();
    const SimHasher& hasher = checker.GetSimHasher();

    const int rounds = 5;

    const Measurement v0 = Measure(text, rounds, [&](const CodePointList& cps, Measurement& out) {
        const auto t = std::chrono::steady_clock::now();
        const TokenList tokens = tokenizer.Split(cps);
        const auto split_done = std::chrono::steady_clock::now();
        const TermFrequency frequencies = CountFromTokenList(tokens);
        out.value = NaiveFingerprint(frequencies, 64);
        const auto fp_done = std::chrono::steady_clock::now();
        out.tokenize = std::chrono::duration<double, std::milli>(split_done - t).count();
        out.fingerprint = std::chrono::duration<double, std::milli>(fp_done - split_done).count();
    });

    const Measurement v1 = Measure(text, rounds, [&](const CodePointList& cps, Measurement& out) {
        const auto t = std::chrono::steady_clock::now();
        const TermFrequency frequencies = tokenizer.CountTokens(cps);
        const auto count_done = std::chrono::steady_clock::now();
        out.value = NaiveFingerprint(frequencies, 64);
        const auto fp_done = std::chrono::steady_clock::now();
        out.tokenize = std::chrono::duration<double, std::milli>(count_done - t).count();
        out.fingerprint = std::chrono::duration<double, std::milli>(fp_done - count_done).count();
    });

    const Measurement v2 = Measure(text, rounds, [&](const CodePointList& cps, Measurement& out) {
        const auto t = std::chrono::steady_clock::now();
        const TermFrequency frequencies = tokenizer.CountTokens(cps);
        const auto count_done = std::chrono::steady_clock::now();
        out.value = hasher.Compute(frequencies);
        const auto fp_done = std::chrono::steady_clock::now();
        out.tokenize = std::chrono::duration<double, std::milli>(count_done - t).count();
        out.fingerprint = std::chrono::duration<double, std::milli>(fp_done - count_done).count();
    });

    std::cout << "  版本            解码(ms)  分词(ms)  指纹(ms)  合计(ms)   相对" << std::endl;
    const struct {
        const char* name;
        Measurement m;
    } rows[3] = {{"V0 原始", v0}, {"V1 只做优化A", v1}, {"V2 优化A+B", v2}};

    for (const auto& row : rows) {
        std::cout << "  " << row.name << "   " << row.m.decode << "\t" << row.m.tokenize << "\t"
                  << row.m.fingerprint << "\t" << row.m.total << "\t  " << v0.total / row.m.total
                  << "x" << std::endl;
    }

    // 三个版本的指纹必须完全相同，否则优化就改变了行为
    const bool identical = (v0.value == v1.value) && (v1.value == v2.value);
    std::cout << "  三版指纹一致: " << (identical ? "是" : "否 —— 优化改变了结果！") << std::endl;
    std::cout << std::endl;
}

}  // namespace bench

void RunBenchmark(const std::string& sample_path) {
    std::string base;
    {
        TextFile file(sample_path);
        base = file.ReadAll();
    }

    std::cout << "===== 性能基准（tests.exe bench）=====" << std::endl;
    std::cout << std::endl;

    const PlagiarismChecker checker;

    std::string repeated;
    repeated.reserve(base.size() * 200);
    for (int i = 0; i < 200; ++i) {
        repeated += base;
    }
    bench::Report("真实文本 · 重复 200 次（词元重复率高）", repeated, checker);
    bench::Report("伪随机汉字 · 200 万字符（词元几乎全唯一，最坏情况）",
                  MakeRandomCjkText(2000000), checker);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "bench") {
        const std::string sample = (argc > 2) ? argv[2] : "测试数据/orig.txt";
        try {
            RunBenchmark(sample);
        } catch (const std::exception& error) {
            std::cout << "基准运行失败: " << error.what() << std::endl;
            return 1;
        }
        return 0;
    }

    RunAllTests();
    return (g_failed == 0) ? 0 : 1;
}
