// 个人项目 · 论文查重（基本功能）
//
// 运行：main.exe <原文文件> <抄袭版论文的文件> <答案文件>
//
// 模块划分（依赖单向，上层依赖下层）：
//   Common.h / Exceptions.h       类型别名与常量、自定义异常
//   UnicodeText.h/.cpp            UTF-8 编解码与字符分类
//   Hash.h/.cpp                   FNV-1a 64 位哈希
//   Tokenizer.h/.cpp              分词（bigram / unigram）
//   SimHasher.h/.cpp              SimHash 指纹与海明距离换算
//   TextFile.h/.cpp               RAII 文件读取
//   AnswerFile.h/.cpp             RAII 答案文件写入
//   PlagiarismChecker.h/.cpp      业务编排（不碰文件系统，便于单元测试）
//   project1.cpp                  入口：解析参数 + 调用 + 统一报错

#include <exception>
#include <iostream>
#include <string>

#ifdef _WIN32
// NOMINMAX：阻止 windows.h 定义 min / max 宏，否则会污染整个工程。
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "AnswerFile.h"
#include "Common.h"
#include "Exceptions.h"
#include "PlagiarismChecker.h"

// 逐条引入而不是 using namespace paper_check;，作用域清晰，也相当于一份依赖说明。
using paper_check::AnswerFile;
using paper_check::AppError;
using paper_check::ArgumentError;
using paper_check::PlagiarismChecker;
using paper_check::SimilarityResult;

namespace {

void PrintUsage() {
    std::cout << "用法: main.exe <原文文件> <抄袭版论文的文件> <答案文件>" << std::endl;
    std::cout << "示例: main.exe D:\\tests\\orig.txt D:\\tests\\orig_add.txt D:\\tests\\ans.txt"
              << std::endl;
}

#ifdef _WIN32
// 源文件和字面量都是 UTF-8，但中文 Windows 控制台默认代码页是 936，会显示成乱码。
// 输出被重定向到管道时这个调用会失败，但那时字节原样传给下游，失败也无所谓。
void EnableUtf8ConsoleOutput() noexcept {
    ::SetConsoleOutputCP(CP_UTF8);
}
#endif

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    EnableUtf8ConsoleOutput();
#endif

    // 所有错误都靠异常上报，这里集中处理。末尾的 catch (...) 是兜底：
    // 未捕获的异常会调用 std::terminate 直接杀进程，而作业规定「异常退出」要扣分。
    try {
        // argv[0] 是程序名，所以三个参数对应 argc == 4。
        // 读 argv[1] 之前必须先把个数不对的情况拦住，否则数组越界。
        if (argc != 4) {
            throw ArgumentError("命令行参数个数不正确，需要 3 个参数，实际收到 " +
                                std::to_string(argc - 1) + " 个");
        }

        const std::string origin_path = argv[1];
        const std::string copy_path = argv[2];
        const std::string answer_path = argv[3];

        // 先算结果，再创建答案文件 —— 顺序不能换，理由见 AnswerFile.h 的注释。
        const PlagiarismChecker checker;
        const SimilarityResult result = checker.CompareFiles(origin_path, copy_path);

        AnswerFile answer_file(answer_path);
        answer_file.WriteSimilarity(result.similarity);

        // 成功时不打印任何东西，唯一的产出就是答案文件（命令行工具的惯例）。
        return 0;
    }
    // catch 顺序必须「先具体、后笼统」：C++ 按书写顺序匹配，
    // 若把 AppError 写在前面，ArgumentError 这个分支永远不会被命中。
    catch (const ArgumentError& error) {
        std::cerr << "错误: " << error.what() << std::endl;
        PrintUsage();
        return 1;
    } catch (const AppError& error) {
        std::cerr << "错误: " << error.what() << std::endl;
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "未预期的错误: " << error.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "未预期的未知错误" << std::endl;
        return 1;
    }
}
