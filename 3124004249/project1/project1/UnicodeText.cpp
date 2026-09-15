#include "UnicodeText.h"

namespace paper_check {
namespace unicode {
namespace {

// UTF-8 的续字节，高两位必须是 10。
bool IsContinuationByte(unsigned char byte) noexcept {
    return (byte & 0xC0U) == 0x80U;
}

}  // namespace

CodePointList Decode(const ByteString& bytes) {
    CodePointList code_points;
    code_points.reserve(bytes.size());

    const unsigned char* cursor = reinterpret_cast<const unsigned char*>(bytes.data());
    const std::size_t total = bytes.size();
    std::size_t index = 0;

    // 各长度序列允许的最小码点值，用来拒绝「过长编码」（overlong encoding）。
    const CodePoint kMinValueForLength[4] = {0x0U, 0x80U, 0x800U, 0x10000U};

    while (index < total) {
        const unsigned char first = cursor[index];
        std::size_t continuation_count = 0;
        CodePoint code_point = 0;

        if (first < 0x80U) {
            code_points.push_back(static_cast<CodePoint>(first));
            ++index;
            continue;
        } else if ((first & 0xE0U) == 0xC0U) {  // 110xxxxx：两字节
            continuation_count = 1;
            code_point = static_cast<CodePoint>(first & 0x1FU);
        } else if ((first & 0xF0U) == 0xE0U) {  // 1110xxxx：三字节，汉字主要走这里
            continuation_count = 2;
            code_point = static_cast<CodePoint>(first & 0x0FU);
        } else if ((first & 0xF8U) == 0xF0U) {  // 11110xxx：四字节
            continuation_count = 3;
            code_point = static_cast<CodePoint>(first & 0x07U);
        } else {  // 非法首字节
            code_points.push_back(kReplacementCharacter);
            ++index;
            continue;
        }

        if (index + continuation_count >= total) {  // 文件在汉字中间被截断
            code_points.push_back(kReplacementCharacter);
            ++index;
            continue;
        }

        bool well_formed = true;
        for (std::size_t offset = 1; offset <= continuation_count; ++offset) {
            const unsigned char byte = cursor[index + offset];
            if (!IsContinuationByte(byte)) {
                well_formed = false;
                break;
            }
            code_point = (code_point << 6) | static_cast<CodePoint>(byte & 0x3FU);
        }
        if (!well_formed) {
            code_points.push_back(kReplacementCharacter);
            ++index;
            continue;
        }

        // 再过两道语义检查：UTF-16 代理区不是合法标量值；大于 U+10FFFF 超出 Unicode 范围。
        const bool is_overlong =
            (continuation_count >= 1) && (code_point < kMinValueForLength[continuation_count]);
        const bool is_surrogate = (code_point >= 0xD800U && code_point <= 0xDFFFU);
        const bool is_out_of_range = (code_point > 0x10FFFFU);
        if (is_overlong || is_surrogate || is_out_of_range) {
            code_points.push_back(kReplacementCharacter);
            ++index;
            continue;
        }

        code_points.push_back(code_point);
        index += continuation_count + 1;
    }

    return code_points;
}

ByteString Encode(CodePoint code_point) {
    ByteString bytes;
    bytes.reserve(4);

    if (code_point < 0x80U) {
        bytes.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800U) {
        bytes.push_back(static_cast<char>(0xC0U | (code_point >> 6)));
        bytes.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point < 0x10000U) {
        bytes.push_back(static_cast<char>(0xE0U | (code_point >> 12)));
        bytes.push_back(static_cast<char>(0x80U | ((code_point >> 6) & 0x3FU)));
        bytes.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        bytes.push_back(static_cast<char>(0xF0U | (code_point >> 18)));
        bytes.push_back(static_cast<char>(0x80U | ((code_point >> 12) & 0x3FU)));
        bytes.push_back(static_cast<char>(0x80U | ((code_point >> 6) & 0x3FU)));
        bytes.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
    return bytes;
}

bool IsCjk(CodePoint code_point) noexcept {
    return (code_point >= 0x4E00U && code_point <= 0x9FFFU) ||
           (code_point >= 0x3400U && code_point <= 0x4DBFU) ||
           (code_point >= 0xF900U && code_point <= 0xFAFFU);
}

bool IsAsciiAlphaNumeric(CodePoint code_point) noexcept {
    const bool is_lower_case = (code_point >= U'a' && code_point <= U'z');
    const bool is_upper_case = (code_point >= U'A' && code_point <= U'Z');
    const bool is_digit = (code_point >= U'0' && code_point <= U'9');
    return is_lower_case || is_upper_case || is_digit;
}

}  // namespace unicode
}  // namespace paper_check
