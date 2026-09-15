// RAII 答案文件写入器：构造即创建并截断文件，析构时 flush + close。
#pragma once

#include <fstream>
#include <string>

namespace paper_check {

class AnswerFile {
public:
    // 构造会创建并清空答案文件，所以要先把结果算出来再构造它 ——
    // 否则计算阶段一旦抛异常，磁盘上会留下一个 0 字节的答案文件。
    explicit AnswerFile(std::string path);
    ~AnswerFile();

    AnswerFile(const AnswerFile&) = delete;
    AnswerFile& operator=(const AnswerFile&) = delete;

    // 写入重复率，保留两位小数。失败抛 OutputFileError。
    void WriteSimilarity(double similarity);

    const std::string& Path() const noexcept { return path_; }

private:
    std::string path_;
    std::ofstream stream_;
};

}  // namespace paper_check
