#include "TextFile.h"

#include <ios>
#include <utility>

#include "Exceptions.h"

namespace paper_check {

TextFile::TextFile(std::string path) : path_(std::move(path)) {
    // 必须用 binary：文本模式会把 "\r\n" 翻译成 "\n"，改变了字节流，
    // 同一份输入在 Windows 和 Linux 下会算出不同结果。
    stream_.open(path_, std::ios::binary);

    if (!stream_.is_open()) {
        throw InputFileError("无法打开原文/抄袭版文件：" + path_);
    }
}

const ByteString& TextFile::ReadAll() {
    if (has_loaded_) {
        return cached_bytes_;
    }

    stream_.seekg(0, std::ios::end);
    const std::streamoff file_size = stream_.tellg();
    if (file_size < 0) {  // 传入目录路径时会走到这里
        throw InputFileError("无法获取文件大小（可能不是普通文件）：" + path_);
    }
    stream_.seekg(0, std::ios::beg);

    cached_bytes_.resize(static_cast<std::size_t>(file_size));
    if (file_size > 0) {  // 空文件不能取 &bytes[0]，那是未定义行为
        stream_.read(&cached_bytes_[0], file_size);
    }

    // 用 gcount 判断是否读满：读到末尾时 eof 也会置位，不能拿 eof 判断成功。
    if (stream_.gcount() != file_size) {
        throw InputFileError("读取文件内容失败（可能被占用或权限不足）：" + path_);
    }

    has_loaded_ = true;
    return cached_bytes_;
}

}  // namespace paper_check
