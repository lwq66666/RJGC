// RAII 文本文件读取器：构造即打开（失败抛异常），析构自动关闭句柄。
#pragma once

#include <fstream>
#include <string>

#include "Common.h"

namespace paper_check {

class TextFile {
public:
    explicit TextFile(std::string path);
    ~TextFile() = default;

    // 禁止拷贝：两个对象持有同一句柄会导致重复关闭。
    // 允许移动：转交所有权，仍然只关闭一次。
    TextFile(const TextFile&) = delete;
    TextFile& operator=(const TextFile&) = delete;
    TextFile(TextFile&&) = default;
    TextFile& operator=(TextFile&&) = default;

    // 读出全部字节。首次调用真正读盘，之后返回缓存。失败抛 InputFileError。
    const ByteString& ReadAll();

    const std::string& Path() const noexcept { return path_; }

private:
    std::string path_;
    std::ifstream stream_;
    ByteString cached_bytes_;
    bool has_loaded_ = false;
};

}  // namespace paper_check
