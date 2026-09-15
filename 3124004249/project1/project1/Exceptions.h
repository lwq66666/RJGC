// 自定义异常层次：下层抛语义明确的异常，main 统一捕获并转成中文提示。
#pragma once

#include <stdexcept>
#include <string>

namespace paper_check {

// 所有「可预期的业务错误」的基类。继承 runtime_error，因为这些都是由外部输入引发的。
class AppError : public std::runtime_error {
public:
    explicit AppError(const std::string& message) : std::runtime_error(message) {}
};

// 命令行参数个数不正确。必须在读 argv[1] 之前拦住，否则数组越界。
class ArgumentError : public AppError {
public:
    explicit ArgumentError(const std::string& message) : AppError(message) {}
};

// 输入文件无法打开或读取失败。
class InputFileError : public AppError {
public:
    explicit InputFileError(const std::string& message) : AppError(message) {}
};

// 答案文件无法创建或写入。
class OutputFileError : public AppError {
public:
    explicit OutputFileError(const std::string& message) : AppError(message) {}
};

// 文档取不出任何词元（空文件，或通篇只有标点）。
// 这里抛异常而不是静默返回：累加器全为 0 时会输出全 0 指纹，
// 两个空文档相比就会得出「重复率 100%」这种假结论。
class EmptyDocumentError : public AppError {
public:
    explicit EmptyDocumentError(const std::string& message) : AppError(message) {}
};

}  // namespace paper_check
