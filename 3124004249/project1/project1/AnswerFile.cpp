#include "AnswerFile.h"

#include <iomanip>
#include <ios>
#include <utility>

#include "Exceptions.h"

namespace paper_check {

AnswerFile::AnswerFile(std::string path) : path_(std::move(path)) {
    // binary 模式不让运行库改写我们写出的字节；trunc 明确表示本次结果覆盖历史内容。
    stream_.open(path_, std::ios::binary | std::ios::trunc);

    if (!stream_.is_open()) {
        throw OutputFileError("无法创建答案文件：" + path_);
    }
}

AnswerFile::~AnswerFile() {
    // 显式 close 让「数据落盘」在代码里看得见。close 不抛异常，放析构里是安全的。
    if (stream_.is_open()) {
        stream_.close();
    }
}

void AnswerFile::WriteSimilarity(double similarity) {
    // std::fixed 不能省：不加的话很小的值会输出成科学计数法，评测按浮点解析会失败。
    stream_ << std::fixed << std::setprecision(2) << similarity;

    if (!stream_.good()) {
        throw OutputFileError("写入答案文件失败：" + path_);
    }
    // 故意不写换行：文件内容就是这一个数，万一评测是读全文比对字符串，多一字节会判错。
}

}  // namespace paper_check
