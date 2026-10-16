#ifndef CLIJUDGE_IDE_H
#define CLIJUDGE_IDE_H

// ide.h
// CLIJudge IDE子命令
//
// 子命令:
//   run [代码路径] [in文件路径] - 运行代码
//
// 实现: 与评测共用 judge::compileSources（可信沙箱编译、输出捕获），
// 运行阶段以结构化 argv 直接进沙箱（不经 cmd.exe / sh），输入文件通过
// SandboxStdio 重定向到 stdin，stdout/stderr 保持继承终端。

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include "platform.h"
#include "settings.h"
#include "judge.h"
#include "lang.h"
#include "sandbox_runner.hpp"

namespace clijudge {
namespace ide {

namespace fs = std::filesystem;

// 由 judge::compileSources 处理的语言扩展（其余扩展名尝试直接执行）
// 自定义语言优先, 可覆盖内置扩展名
inline bool isKnownLanguage(const std::string& ext) {
    return clijudge::settings::findByExtension(ext) != nullptr
        || ext == ".py" || ext == ".js" || ext == ".cpp" || ext == ".cc"
        || ext == ".cxx" || ext == ".c" || ext == ".java";
}

// 创建临时工作目录
inline std::string createWorkDir() {
    static unsigned int seq = 0;
    std::string name = "clijudge_ide_" + std::to_string(platform::pid()) + "_"
                     + std::to_string(platform::tickMs()) + "_"
                     + std::to_string(seq++);
    std::string workDir = platform::pathJoin(platform::tempDir(), name);
    fs::create_directories(workDir);
    return workDir;
}

// 清理临时目录
inline void cleanupWorkDir(const std::string& workDir) {
    try {
        if (fs::exists(workDir)) {
            fs::remove_all(workDir);
        }
    } catch (...) {
        // 忽略清理错误
    }
}

// 运行代码
inline int cmdRun(const std::string& codePath, const std::string& inputPath = "") {
    // 验证代码文件存在
    if (!fs::exists(codePath)) {
        std::cerr << clijudge::lang::trf("ide.code_file_not_found", "Error: code file not found: {0}", {codePath}) << std::endl;
        return 1;
    }

    // 创建临时工作目录
    std::string workDir = createWorkDir();
    std::string metaFile = platform::pathJoin(workDir, "_meta.json");
    std::string absCode = fs::absolute(codePath).string();

    std::string ext = fs::path(codePath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // 编译/解析运行命令 (复用评测链路: 可信沙箱, 错误输出捕获后打印)
    std::string exePath;
    std::vector<std::string> exeArgs;
    if (isKnownLanguage(ext)) {
        auto cr = clijudge::judge::compileSources({absCode}, workDir, "output");
        if (!cr.success) {
            // compileSources 内部已把错误打到 stderr
            cleanupWorkDir(workDir);
            return 1;
        }
        exePath = cr.exePath;
        exeArgs = cr.exeArgs;
    } else {
        // 未知扩展名: 尝试直接执行 (兼容脚本/可执行文件)
        exePath = absCode;
    }

    // 标准句柄: 有输入文件时 stdin 来自文件, stdout/stderr 仍到终端;
    // 无输入文件时全部继承 (可交互)
    SandboxStdio io;
    bool useFileIn = !inputPath.empty() && fs::exists(inputPath);
    io.inherit = !useFileIn;
    if (useFileIn) {
        io.stdinPath = fs::absolute(inputPath).string();
#ifdef _WIN32
        io.hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        io.hStderr = GetStdHandle(STD_ERROR_HANDLE);
#endif
    }

    // 沙箱运行 (无 shell: 结构化 argv, 单进程)
    auto result = clijudge::sandbox_run(
        10000,  // 10秒超时
        256,    // 256MB内存限制
        1,      // 单进程 (不再经过 shell 复合命令)
        metaFile.c_str(),
        exePath.c_str(),
        exeArgs,
        false,
        useFileIn ? &io : nullptr,
        fs::absolute(workDir).string(),
        clijudge::settings::extraEnvList(),
        0,
        false
    );

    // 输出结果
    if (result.success) {
        std::cout << clijudge::lang::trf("ide.exit_code", "Exit code: {0}", {std::to_string(result.exitCode)}) << std::endl;
        std::cout << clijudge::lang::trf("ide.time", "Time: {0} ms", {std::to_string(result.timeUsedMs)}) << std::endl;
        std::cout << clijudge::lang::trf("ide.memory", "Memory: {0} KB", {std::to_string(result.memoryUsedKB)}) << std::endl;
        if (strcmp(result.signal, "null") != 0) {
            std::cout << clijudge::lang::trf("ide.signal", "Signal: {0}", {result.signal}) << std::endl;
        }
    } else {
        std::cerr << clijudge::lang::tr("ide.sandbox_failed", "Sandbox failed to run the code.") << std::endl;
    }

    // 清理临时目录
    cleanupWorkDir(workDir);

    return result.success ? result.exitCode : 1;
}

} // namespace ide
} // namespace clijudge

#endif // CLIJUDGE_IDE_H
