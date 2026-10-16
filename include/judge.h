#ifndef CLIJUDGE_JUDGE_H
#define CLIJUDGE_JUDGE_H

// judge.h
// CliJudge 评判核心模块 —— 对齐 LemonLime 判定语义
//
// 支持的比较模式 (compare_mode):
//   text_strict   - 逐字节严格比较 (CRLF/LF 归一化)
//   text_line     - 逐行比较 (忽略行末空白) —— LemonLime LineByLine 语义:
//                   标准答案还有内容而选手先结束 → WA "less contents";
//                   选手还有内容而标准答案先结束 → OLE "too much contents"
//   text_no_space - 忽略空白按 token 比较 —— LemonLime IgnoreSpaces 语义:
//                   token 全等但行结构不同 → PE; 内容不同 → WA; 多余 → OLE
//   float_abs     - 实数绝对误差 (超出 absTol 判错)
//   float_rel     - 实数相对误差 (相对标准答案, 超出 relTol 判错)
//   float_all     - 实数综合误差 (|Δ|>absTol 且 |Δ|>relTol*|标准| 才判错)
//                   未设置容差时默认 1e-6; NaN/Inf 必须同型
//   spj           - 自定义 Special Judge: argv = (input, 选手输出, 标准输出),
//                   exit 0 → AC, 否则 WA; 超时 → ST
//   spj_lemon     - LemonLime 6 参数 SPJ: argv += (fullScore, scoreFile, msgFile)
//                   超时 → ST; 非 0 退出 → SR; scoreFile 损坏/负分 → IV;
//                   0 → WA; 0<x<full → PC; ≥full → AC
//   spj_testlib   - testlib SPJ: argv = (input, 选手输出, 标准输出), 解析 stderr:
//                   "ok" → AC; "FAIL" → IV; "partially correct (N)" → N*full/100;
//                   "points X" → X*full; "wrong ..." → WA; 其余 → 0 分 WA;
//                   不检查退出码
//
// 子任务 (subtask_mode): simple/group = 求和; all_or_nothing = 取最小分
//   problem.subtask_dependence: {"<subtaskId>": [depId, ...]} 依赖子任务:
//   依赖得分为 0 或 SKIPPED → 被依赖者 SKIPPED; 依赖部分得分 → 按比例封顶
//
// 其它: 源码大小上限 / 编译超时 / 评测线程数 / 边界 TLE 自动重跑
//       (rejudge_times) 均取自 settings.h (config.json 的 "judge" 段)

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <map>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <thread>
#include <atomic>
#include <utility>
#include <chrono>
#include <future>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#endif
#include "json.hpp"
#include "platform.h"
#include "sandbox_runner.hpp"
#include "settings.h"
#include "lang.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "miniz/miniz.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace clijudge {
namespace judge {

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── 评判结果状态 ────────────────────────────────────────────
enum class JudgeStatus {
    ACCEPTED,
    WRONG_ANSWER,
    TIME_LIMIT_EXCEEDED,
    MEMORY_LIMIT_EXCEEDED,
    RUNTIME_ERROR,
    COMPILATION_ERROR,
    SYSTEM_ERROR,
    SKIPPED,
    PRESENTATION_ERROR,
    OUTPUT_LIMIT_EXCEEDED,
    PARTIALLY_CORRECT,
    INVALID_SPJ,
    SPECIAL_JUDGE_TLE,
    SPECIAL_JUDGE_RE,
    INTERACTOR_ERROR
};

// 单个测试点结果
struct TestCaseResult {
    int id = 0;
    int score = 0;
    int maxScore = 0;
    JudgeStatus status = JudgeStatus::SYSTEM_ERROR;
    int timeUsedMs = 0;
    int memoryUsedKB = 0;
    std::string message;
};

// 子任务结果
struct SubtaskResult {
    int id = 0;
    int score = 0;
    int maxScore = 0;
    JudgeStatus status = JudgeStatus::ACCEPTED;
    std::vector<TestCaseResult> testCases;
};

// 完整评判结果
struct JudgeResult {
    int totalScore = 0;
    int maxScore = 0;
    JudgeStatus status = JudgeStatus::ACCEPTED;
    std::vector<SubtaskResult> subtasks;
    std::string compileError;
    int totalTimeMs = 0;
    int maxMemoryKB = 0;
};

// 状态转字符串
inline std::string statusToString(JudgeStatus s) {
    switch (s) {
        case JudgeStatus::ACCEPTED: return "Accepted";
        case JudgeStatus::WRONG_ANSWER: return "Wrong Answer";
        case JudgeStatus::TIME_LIMIT_EXCEEDED: return "Time Limit Exceeded";
        case JudgeStatus::MEMORY_LIMIT_EXCEEDED: return "Memory Limit Exceeded";
        case JudgeStatus::RUNTIME_ERROR: return "Runtime Error";
        case JudgeStatus::COMPILATION_ERROR: return "Compilation Error";
        case JudgeStatus::SYSTEM_ERROR: return "System Error";
        case JudgeStatus::SKIPPED: return "Skipped";
        case JudgeStatus::PRESENTATION_ERROR: return "Presentation Error";
        case JudgeStatus::OUTPUT_LIMIT_EXCEEDED: return "Output Limit Exceeded";
        case JudgeStatus::PARTIALLY_CORRECT: return "Partially Correct";
        case JudgeStatus::INVALID_SPJ: return "Invalid Special Judge";
        case JudgeStatus::SPECIAL_JUDGE_TLE: return "Special Judge Time Limit Exceeded";
        case JudgeStatus::SPECIAL_JUDGE_RE: return "Special Judge Runtime Error";
        case JudgeStatus::INTERACTOR_ERROR: return "Interactor Error";
        default: return "Unknown";
    }
}

// 状态转缩写 (已发布的缩写不可更改, 提交记录依赖它们)
inline std::string statusToAbbr(JudgeStatus s) {
    switch (s) {
        case JudgeStatus::ACCEPTED: return "AC";
        case JudgeStatus::WRONG_ANSWER: return "WA";
        case JudgeStatus::TIME_LIMIT_EXCEEDED: return "TLE";
        case JudgeStatus::MEMORY_LIMIT_EXCEEDED: return "MLE";
        case JudgeStatus::RUNTIME_ERROR: return "RE";
        case JudgeStatus::COMPILATION_ERROR: return "CE";
        case JudgeStatus::SYSTEM_ERROR: return "SE";
        case JudgeStatus::SKIPPED: return "SK";
        case JudgeStatus::PRESENTATION_ERROR: return "PE";
        case JudgeStatus::OUTPUT_LIMIT_EXCEEDED: return "OLE";
        case JudgeStatus::PARTIALLY_CORRECT: return "PC";
        case JudgeStatus::INVALID_SPJ: return "IV";
        case JudgeStatus::SPECIAL_JUDGE_TLE: return "ST";
        case JudgeStatus::SPECIAL_JUDGE_RE: return "SR";
        case JudgeStatus::INTERACTOR_ERROR: return "IE";
        default: return "??";
    }
}

// 状态转本地化字符串（用于控制台显示；statusToString/statusToAbbr 保持稳定供数据与缩写使用）
inline std::string statusToDisplay(JudgeStatus s) {
    std::string fallback = statusToString(s);
    const char* key = nullptr;
    switch (s) {
        case JudgeStatus::ACCEPTED: key = "judge.accepted"; break;
        case JudgeStatus::WRONG_ANSWER: key = "judge.wrong_answer"; break;
        case JudgeStatus::TIME_LIMIT_EXCEEDED: key = "judge.time_limit"; break;
        case JudgeStatus::MEMORY_LIMIT_EXCEEDED: key = "judge.memory_limit"; break;
        case JudgeStatus::RUNTIME_ERROR: key = "judge.runtime_error"; break;
        case JudgeStatus::COMPILATION_ERROR: key = "judge.compile_error"; break;
        case JudgeStatus::SYSTEM_ERROR: key = "judge.system_error"; break;
        case JudgeStatus::SKIPPED: key = "judge.skipped"; break;
        case JudgeStatus::PRESENTATION_ERROR: key = "judge.presentation_error"; break;
        case JudgeStatus::OUTPUT_LIMIT_EXCEEDED: key = "judge.output_limit"; break;
        case JudgeStatus::PARTIALLY_CORRECT: key = "judge.partially_correct"; break;
        case JudgeStatus::INVALID_SPJ: key = "judge.invalid_spj"; break;
        case JudgeStatus::SPECIAL_JUDGE_TLE: key = "judge.special_judge_tle"; break;
        case JudgeStatus::SPECIAL_JUDGE_RE: key = "judge.special_judge_runtime"; break;
        case JudgeStatus::INTERACTOR_ERROR: key = "judge.interactor_error"; break;
        default: break;
    }
    if (key) {
        return clijudge::lang::tr(key, fallback);
    }
    return fallback;
}

// ── 文件读取工具 ─────────────────────────────────────────────
inline std::string readFileContent(const std::string& path) {
    if (!fs::exists(path)) return "";
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// 写文件
inline bool writeFileContent(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(content.data(), (std::streamsize)content.size());
    return f.good();
}

// 去除行末空白
inline std::string trimLineEnd(const std::string& s) {
    std::string result = s;
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n' ||
                               result.back() == ' ' || result.back() == '\t')) {
        result.pop_back();
    }
    return result;
}

// CRLF → LF 归一化
inline std::string normalizeEol(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
        r += s[i];
    }
    return r;
}

// 读取沙箱元数据 (容忍缺失/损坏)
inline json readMetaFile(const std::string& metaFile) {
    if (!fs::exists(metaFile)) return json();
    try {
        std::ifstream f(metaFile);
        json j;
        f >> j;
        if (j.is_object()) return j;
    } catch (...) {}
    return json();
}

// 按行拆分 (去掉行尾 \r; 文件末尾换行不产生额外空行)
inline std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : s) {
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    lines.push_back(cur);
    if (lines.size() > 1 && lines.back().empty()) lines.pop_back();
    return lines;
}

// 按行拆分并去除行末空白
inline std::vector<std::string> splitLinesTrim(const std::string& s) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : s) {
        if (c == '\n') {
            lines.push_back(trimLineEnd(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    lines.push_back(trimLineEnd(cur));
    if (lines.size() > 1 && lines.back().empty()) lines.pop_back();
    return lines;
}

// token + 所在行号
struct TokenWithLine {
    std::string text;
    int line = 1;
};

inline std::vector<TokenWithLine> tokenizeWithLines(const std::string& s) {
    std::vector<TokenWithLine> out;
    int line = 1;
    std::string cur;
    auto flush = [&]() {
        if (!cur.empty()) {
            out.push_back({cur, line});
            cur.clear();
        }
    };
    for (char c : s) {
        if (c == '\n') {
            flush();
            line++;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            flush();
        } else {
            cur += c;
        }
    }
    flush();
    return out;
}

// ── 比较结果 ─────────────────────────────────────────────────
struct CompareResult {
    JudgeStatus status = JudgeStatus::SYSTEM_ERROR;
    int score = 0;
    std::string message;
};

inline CompareResult mkCompare(JudgeStatus st, int score, const std::string& msg) {
    CompareResult r;
    r.status = st;
    r.score = score;
    r.message = msg;
    return r;
}

// ── 比较函数 (返回 CompareResult, 对齐 LemonLime 判定) ───────

// 严格文本比较 (CRLF/LF 统一)
inline CompareResult compareStrict(const std::string& expected, const std::string& actual, int maxScore) {
    if (normalizeEol(expected) == normalizeEol(actual))
        return mkCompare(JudgeStatus::ACCEPTED, maxScore, "Correct");
    return mkCompare(JudgeStatus::WRONG_ANSWER, 0, "Wrong Answer");
}

// 逐行比较 (忽略行末空白) —— LineByLine: 少内容 WA, 多内容 OLE
inline CompareResult compareLineByLine(const std::string& expected, const std::string& actual, int maxScore) {
    std::vector<std::string> e = splitLinesTrim(expected);
    std::vector<std::string> a = splitLinesTrim(actual);
    size_t n = std::min(e.size(), a.size());
    for (size_t i = 0; i < n; i++) {
        if (e[i] != a[i])
            return mkCompare(JudgeStatus::WRONG_ANSWER, 0,
                             "Wrong Answer on line " + std::to_string(i + 1));
    }
    if (a.size() < e.size())
        return mkCompare(JudgeStatus::WRONG_ANSWER, 0, "Wrong Answer: less contents");
    if (a.size() > e.size())
        return mkCompare(JudgeStatus::OUTPUT_LIMIT_EXCEEDED, 0, "Output Limit Exceeded: too much contents");
    return mkCompare(JudgeStatus::ACCEPTED, maxScore, "Correct");
}

// 忽略空白按 token 比较 —— IgnoreSpaces: token 全等但行结构不同 → PE
inline CompareResult compareIgnoreSpace(const std::string& expected, const std::string& actual, int maxScore) {
    std::vector<TokenWithLine> e = tokenizeWithLines(expected);
    std::vector<TokenWithLine> a = tokenizeWithLines(actual);
    size_t n = std::min(e.size(), a.size());
    for (size_t i = 0; i < n; i++) {
        if (e[i].text != a[i].text)
            return mkCompare(JudgeStatus::WRONG_ANSWER, 0,
                             "Wrong Answer on line " + std::to_string(e[i].line));
    }
    if (e.size() > a.size())
        return mkCompare(JudgeStatus::WRONG_ANSWER, 0, "Wrong Answer: less contents");
    if (a.size() > e.size())
        return mkCompare(JudgeStatus::OUTPUT_LIMIT_EXCEEDED, 0, "Output Limit Exceeded: too much contents");
    // token 全等但行分组不同 → PE: 逐 token 比较所在行 (只数含 token 的行数会漏掉
    // "1\n2 3" 与 "1 2\n3" 这类同行数不同分组的差异)
    for (size_t i = 0; i < e.size(); i++) {
        if (e[i].line != a[i].line)
            return mkCompare(JudgeStatus::PRESENTATION_ERROR, 0,
                             "Presentation Error: unexpected end of line");
    }
    return mkCompare(JudgeStatus::ACCEPTED, maxScore, "Correct");
}

// 宽字符解析为实数 (完整消耗 token 才算数字)
inline bool parseLongDouble(const std::string& tok, long double& out) {
    if (tok.empty()) return false;
    const char* s = tok.c_str();
    char* end = nullptr;
    errno = 0;
    long double v = strtold(s, &end);
    if (end == s || *end != '\0') return false;
    (void)v;
    out = v;
    return true;
}

// 实数比较 —— RealNumber: 按模式取容差, NaN/Inf 必须同型;
// 少内容 WA, 多内容 OLE
inline CompareResult compareFloat(const std::string& expected, const std::string& actual,
                                  int maxScore, const std::string& mode,
                                  double absTol, double relTol) {
    std::vector<std::string> et, at;
    for (const auto& t : tokenizeWithLines(expected)) et.push_back(t.text);
    for (const auto& t : tokenizeWithLines(actual)) at.push_back(t.text);

    size_t n = std::min(et.size(), at.size());
    for (size_t i = 0; i < n; i++) {
        long double ev = 0, av = 0;
        bool eNum = parseLongDouble(et[i], ev);
        bool aNum = parseLongDouble(at[i], av);
        if (!eNum || !aNum) {
            if (et[i] != at[i])
                return mkCompare(JudgeStatus::WRONG_ANSWER, 0,
                                 "Wrong Answer on token " + std::to_string(i + 1));
            continue;
        }
        if (std::isnan(ev) != std::isnan(av))
            return mkCompare(JudgeStatus::WRONG_ANSWER, 0,
                             "Wrong Answer on token " + std::to_string(i + 1));
        if (std::isnan(ev) && std::isnan(av)) continue;
        if (std::isinf(ev) != std::isinf(av) ||
            (std::isinf(ev) && std::isinf(av) && ((ev > 0) != (av > 0))))
            return mkCompare(JudgeStatus::WRONG_ANSWER, 0,
                             "Wrong Answer on token " + std::to_string(i + 1));
        if (std::isinf(ev)) continue;

        long double diff = std::fabs(ev - av);
        bool bad;
        if (mode == "float_abs") {
            double eps = absTol > 0.0 ? absTol : 1e-6;
            bad = diff > eps;
        } else if (mode == "float_rel") {
            double eps = relTol > 0.0 ? relTol : 1e-6;
            bad = diff > eps * std::fabs(ev);
        } else {
            double ea = absTol > 0.0 ? absTol : 1e-6;
            double er = relTol > 0.0 ? relTol : 1e-6;
            bad = diff > ea && diff > er * std::fabs(ev);
        }
        if (bad)
            return mkCompare(JudgeStatus::WRONG_ANSWER, 0,
                             "Wrong Answer on token " + std::to_string(i + 1));
    }
    if (et.size() > at.size())
        return mkCompare(JudgeStatus::WRONG_ANSWER, 0, "Wrong Answer: less contents");
    if (at.size() > et.size())
        return mkCompare(JudgeStatus::OUTPUT_LIMIT_EXCEEDED, 0, "Output Limit Exceeded: too much contents");
    return mkCompare(JudgeStatus::ACCEPTED, maxScore, "Correct");
}

// 按 compare_mode 分发非 SPJ 比较
inline CompareResult compareOutputs(const std::string& mode, const std::string& expected,
                                    const std::string& actual, int maxScore,
                                    double absTol, double relTol) {
    if (mode == "text_line") return compareLineByLine(expected, actual, maxScore);
    if (mode == "text_no_space") return compareIgnoreSpace(expected, actual, maxScore);
    if (mode == "float_abs" || mode == "float_rel" || mode == "float_all")
        return compareFloat(expected, actual, maxScore, mode, absTol, relTol);
    return compareStrict(expected, actual, maxScore);
}

// ── 编译 ─────────────────────────────────────────────────────
struct CompileResult {
    bool success = false;
    std::string exePath;
    std::vector<std::string> exeArgs;
    std::string error;
    std::string language;
};

// 获取语言名称 (自定义语言优先, 其余为内置)
inline std::string getLanguageName(const std::string& ext) {
    if (const auto* cl = settings::findByExtension(ext)) return cl->name;
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") return "C++";
    if (ext == ".c") return "C";
    if (ext == ".py") return "Python";
    if (ext == ".java") return "Java";
    if (ext == ".js") return "JavaScript";
    return "Unknown";
}

// 获取可执行文件扩展名
inline std::string getExeExtension() {
    return platform::exeSuffix();
}

// 检查是否为可执行文件 (按扩展名, Windows)
inline bool isExecutable(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".exe" || ext == ".com" || ext == ".bat" || ext == ".cmd";
}

#ifndef _WIN32
// 检查是否为 ELF 可执行文件 (Linux 上按文件头判断)
inline bool isElfExecutable(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    char m[4] = { 0, 0, 0, 0 };
    f.read(m, 4);
    return f.gcount() == 4 && m[0] == '\x7f' && m[1] == 'E' && m[2] == 'L' && m[3] == 'F';
}
#endif

// 检查是否为脚本语言
inline bool isScript(const std::string& ext) {
    return ext == ".py" || ext == ".js";
}

// 自定义语言装配 (config.json judge.custom_languages):
// compile 非空 → 可信执行编译模板 (同编译器语义) 再执行 run;
// compile 为空 → 脚本式, 直接执行 run. 产物缺失只在 run 引用 {exe} 时报错。
inline CompileResult compileCustomLanguage(const settings::CustomLanguage& lang,
                                           const std::vector<std::string>& sources,
                                           const std::string& workDir,
                                           const std::string& exeName) {
    CompileResult result;
    result.language = lang.name;
    fs::create_directories(workDir);
    std::string exePath = platform::pathJoin(workDir, exeName + getExeExtension());
    std::string cwd = fs::absolute(workDir).string();

    auto expand = [&](const std::string& tpl) {
        return settings::expandCommand(settings::tokenizeCommand(tpl), sources, exePath, cwd);
    };

    if (lang.compile.empty()) {
        auto argv = expand(lang.run);
        if (argv.empty()) {
            result.error = "Custom language '" + lang.name + "' has empty run command";
            std::cerr << result.error << std::endl;
            return result;
        }
        result.exePath = argv.front();
        result.exeArgs.assign(argv.begin() + 1, argv.end());
        result.success = true;
        return result;
    }

    // ── 编译阶段 (可信运行, 捕获输出) ──
    auto cArgv = expand(lang.compile);
    if (cArgv.empty()) {
        result.error = "Custom language '" + lang.name + "' has empty compile command";
        std::cerr << result.error << std::endl;
        return result;
    }
    std::string compiler = cArgv.front();
    std::vector<std::string> compilerArgs(cArgv.begin() + 1, cArgv.end());

    std::string outFile = platform::pathJoin(workDir, "_cc_stdout.txt");
    std::string errFile = platform::pathJoin(workDir, "_cc_stderr.txt");
    std::string metaFile = platform::pathJoin(workDir, "_cc_meta.json");
    std::string inDummy = platform::pathJoin(workDir, "_cc_stdin.txt");
    if (!writeFileContent(inDummy, "")) {
        result.error = "System Error: cannot write compile input file";
        return result;
    }

    SandboxStdio io;
    io.inherit = false;
    io.stdinPath = inDummy;
    io.stdoutPath = outFile;
    io.stderrPath = errFile;

    clijudge::sandbox_run(
        settings::getCompileTimeLimit(),
        0,
        64,
        metaFile.c_str(),
        compiler.c_str(),
        compilerArgs,
        false,
        &io,
        cwd,
        settings::extraEnvList(),
        0,
        true   // 同编译器: 可信运行
    );

    json meta = readMetaFile(metaFile);
    if (meta.is_null()) {
        result.error = "System Error: compiler did not run";
        std::cerr << clijudge::lang::trf("judge.compile_system_error", "[compile] system error: {0}", {compiler}) << std::endl;
        return result;
    }
    std::string signal = meta.value("signal", "null");
    int exitCode = meta.value("exit_code", 1);
    std::string output = readFileContent(outFile) + readFileContent(errFile);
    if (output.size() > 16384) output = output.substr(0, 16384) + "\n... (truncated)";

    if (signal == "SIGKILL") {
        result.error = "Compilation Time Limit Exceeded";
        std::cerr << clijudge::lang::tr("judge.compile_time_limit", "[compile] time limit exceeded") << std::endl;
        return result;
    }
    if (signal == "PROC_LIMIT") {
        result.error = "Compilation failed: process limit exceeded";
        std::cerr << clijudge::lang::tr("judge.compile_proc_limit", "[compile] process limit exceeded") << std::endl;
        return result;
    }
    if (signal == "SYSTEM_ERROR") {
        result.error = "System Error during compilation";
        std::cerr << clijudge::lang::tr("judge.compile_system_error_plain", "[compile] system error") << std::endl;
        return result;
    }
    if (exitCode != 0) {
        result.error = output.empty()
            ? ("Compilation failed (exit code: " + std::to_string(exitCode) + ")")
            : output;
        std::cerr << result.error << std::endl;
        return result;
    }

    // ── 运行命令 ──
    std::string runTpl = lang.run;
    bool usesExe = runTpl.find("{exe}") != std::string::npos;
    auto rArgv = expand(runTpl);
    if (rArgv.empty()) {
        result.error = "Custom language '" + lang.name + "' has empty run command";
        std::cerr << result.error << std::endl;
        return result;
    }
    if (usesExe && !fs::exists(exePath)) {
        result.error = "Compilation produced no output";
        std::cerr << clijudge::lang::tr("judge.compile_no_output", "[compile] produced no output") << std::endl;
        return result;
    }
    result.exePath = rArgv.front();
    result.exeArgs.assign(rArgv.begin() + 1, rArgv.end());
    result.success = true;
    return result;
}

// 编译 (沙箱化: 编译超时 = settings.compile_time_limit_ms, 捕获输出)
// sources: 源文件列表 (通信题可多个, 第一个决定语言)
// exeName: 输出可执行文件名 (不含扩展名, 默认 "program")
// withPthread: 链接 -pthread (communication_exec 的 grader, 对应 LemonLime)
inline CompileResult compileSources(const std::vector<std::string>& sources,
                                    const std::string& workDir,
                                    const std::string& exeName = "program",
                                    bool withPthread = false) {
    CompileResult result;
    result.success = false;
    if (sources.empty()) {
        result.error = "No source files";
        return result;
    }
    for (const auto& s : sources) {
        if (!fs::exists(s)) {
            result.error = "Source file not found: " + s;
            return result;
        }
    }

    std::string ext = fs::path(sources.front()).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    result.language = getLanguageName(ext);

    fs::create_directories(workDir);

    // 自定义语言优先于内置 (可覆盖 .py/.cpp/.java 等内置扩展名)
    if (const auto* cl = settings::findByExtension(ext)) {
        return compileCustomLanguage(*cl, sources, workDir, exeName);
    }

    // 脚本语言不需要编译
    if (ext == ".py" || ext == ".js") {
#ifdef _WIN32
        result.exePath = (ext == ".py") ? "python" : "node";
#else
        result.exePath = (ext == ".py") ? "python3" : "node";
#endif
        result.exeArgs.push_back(fs::absolute(sources.front()).string());
        result.success = true;
        return result;
    }

    std::string exePath;
    std::vector<std::string> argv;
    std::string compiler;

    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c") {
        compiler = (ext == ".c") ? "gcc" : "g++";
        exePath = platform::pathJoin(workDir, exeName + getExeExtension());
        argv.push_back("-O2");
        std::string sf = platform::staticLinkFlag();
        if (!sf.empty()) {
            // staticLinkFlag 返回 " -static" 或 ""
            size_t b = sf.find_first_not_of(' ');
            if (b != std::string::npos) argv.push_back(sf.substr(b));
        }
#ifndef _WIN32
        if (withPthread) argv.push_back("-pthread");
#endif
        argv.push_back("-o");
        argv.push_back(exePath);
        for (const auto& s : sources) argv.push_back(fs::absolute(s).string());
    } else if (ext == ".java") {
        compiler = "javac";
        exePath = "java";
        argv.push_back("-d");
        argv.push_back(fs::absolute(workDir).string());
        for (const auto& s : sources) argv.push_back(fs::absolute(s).string());
    } else {
        result.error = "Unsupported language: " + ext;
        return result;
    }

    // 沙箱执行编译 (文件 stdio, 不限制输出大小)
    std::string ioDir = workDir;
    fs::create_directories(ioDir);
    std::string outFile = platform::pathJoin(ioDir, "_cc_stdout.txt");
    std::string errFile = platform::pathJoin(ioDir, "_cc_stderr.txt");
    std::string metaFile = platform::pathJoin(ioDir, "_cc_meta.json");
    std::string inDummy = platform::pathJoin(ioDir, "_cc_stdin.txt");
    if (!writeFileContent(inDummy, "")) {
        result.error = "System Error: cannot write compile input file";
        return result;
    }

    SandboxStdio io;
    io.inherit = false;
    io.stdinPath = inDummy;
    io.stdoutPath = outFile;
    io.stderrPath = errFile;

    std::string cwd = fs::absolute(workDir).string();
    auto sr = clijudge::sandbox_run(
        settings::getCompileTimeLimit(),
        0,    // 不限制编译器内存
        64,   // LemonLime 编译进程上限
        metaFile.c_str(),
        compiler.c_str(),
        argv,
        false,
        &io,
        cwd,
        settings::extraEnvList(),
        0,
        true   // 编译器需要在临时目录写文件: 可信运行
    );
    (void)sr;

    json meta = readMetaFile(metaFile);
    if (meta.is_null()) {
        result.error = "System Error: compiler did not run";
        std::cerr << clijudge::lang::trf("judge.compile_system_error", "[compile] system error: {0}", {compiler}) << std::endl;
        return result;
    }
    std::string signal = meta.value("signal", "null");
    int exitCode = meta.value("exit_code", 1);
    std::string output = readFileContent(outFile) + readFileContent(errFile);
    if (output.size() > 16384) output = output.substr(0, 16384) + "\n... (truncated)";

    if (signal == "SIGKILL") {
        result.error = "Compilation Time Limit Exceeded";
        std::cerr << clijudge::lang::tr("judge.compile_time_limit", "[compile] time limit exceeded") << std::endl;
        return result;
    }
    if (signal == "PROC_LIMIT") {
        result.error = "Compilation failed: process limit exceeded";
        std::cerr << clijudge::lang::tr("judge.compile_proc_limit", "[compile] process limit exceeded") << std::endl;
        return result;
    }
    if (signal == "SYSTEM_ERROR") {
        result.error = "System Error during compilation";
        std::cerr << clijudge::lang::tr("judge.compile_system_error_plain", "[compile] system error") << std::endl;
        return result;
    }
    if (exitCode != 0) {
        result.error = output.empty()
            ? ("Compilation failed (exit code: " + std::to_string(exitCode) + ")")
            : output;
        std::cerr << result.error << std::endl;
        return result;
    }

    // 验证编译产物
    if (ext == ".java") {
        std::string mainClass = fs::path(sources.front()).stem().string();
        std::string classFile = platform::pathJoin(workDir, mainClass + ".class");
        if (!fs::exists(classFile)) {
            result.error = "Compilation produced no output";
            std::cerr << clijudge::lang::tr("judge.compile_no_output", "[compile] produced no output") << std::endl;
            return result;
        }
        // 运行: java -cp <workDir> <MainClass>
        result.exePath = "java";
        result.exeArgs = {"-cp", fs::absolute(workDir).string(), mainClass};
    } else {
        if (!fs::exists(exePath)) {
            result.error = "Compilation produced no output";
            std::cerr << clijudge::lang::tr("judge.compile_no_output", "[compile] produced no output") << std::endl;
            return result;
        }
        result.exePath = exePath;
    }

    result.success = true;
    return result;
}

// Special Judge 编译/解析: spj_code (内嵌源码) 或 special_judge_exe (已有可执行文件)
inline CompileResult compileSpecialJudge(const json& problem, const std::string& workDir) {
    CompileResult result;
    std::string spjCode;
    std::string spjExe;
    if (problem.contains("problem") && problem["problem"].is_object()) {
        spjCode = problem["problem"].value("spj_code", "");
        spjExe = problem["problem"].value("special_judge_exe", "");
    }
    if (!spjCode.empty()) {
        std::string src = platform::pathJoin(workDir, "spj_check.cpp");
        if (!writeFileContent(src, spjCode)) {
            result.error = "System Error: cannot write special judge source";
            return result;
        }
        return compileSources({src}, workDir, "spj_check");
    }
    if (!spjExe.empty()) {
        std::error_code ec;
        std::string abs = fs::absolute(spjExe, ec).string();
        if (!ec && fs::exists(abs)) {
            result.success = true;
            result.exePath = abs;
            result.language = "Special Judge";
            return result;
        }
        result.error = "Special judge executable not found: " + spjExe;
        return result;
    }
    result.error = "No special judge defined (spj_code / special_judge_exe is empty)";
    return result;
}

// ── ZIP 解压 (answers_only 作答包) ───────────────────────────
// 任一条目读取/建目录/写文件失败 → 返回 false (调用方判 SYSTEM_ERROR),
// 绝不能吞掉错误继续: 否则作答包部分丢失会被当成"选手答案为空"误判 WA
inline bool extractZipToDir(const std::string& zipPath, const std::string& destDir) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)) return false;
    bool ok = true;
    int n = (int)mz_zip_reader_get_num_files(&zip);
    for (int i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) { ok = false; continue; }
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        std::string name = st.m_filename;
        std::replace(name.begin(), name.end(), '\\', '/');
        if (name.empty() || name[0] == '/' || name.find(':') != std::string::npos ||
            name.find("..") != std::string::npos) continue;
        size_t sz = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
        if (!data) { ok = false; continue; }
        std::error_code ec;
        fs::path out = fs::path(destDir) / name;
        fs::create_directories(out.parent_path(), ec);
        if (ec) { mz_free(data); ok = false; continue; }
        {
            std::ofstream f(out, std::ios::binary);
            if (!f.is_open()) {
                ok = false;
            } else {
                f.write((const char*)data, (std::streamsize)sz);
                if (!f.good()) ok = false;
            }
        }
        mz_free(data);
    }
    mz_zip_reader_end(&zip);
    return ok;
}

// ── 运行选手程序 ─────────────────────────────────────────────
struct RunOutcome {
    JudgeStatus status = JudgeStatus::SYSTEM_ERROR;
    int exitCode = 0;
    int timeUsedMs = 0;
    int memoryUsedKB = 0;
    std::string output;  // stdout
    std::string error;   // stderr
    std::string message;
};

// LemonLime killLimit: 先给 ceil(timeLimit/1000)*1000 的墙钟,
// 再加 max(2000, 2*timeLimit)*extraTimeRatio 的宽限
inline int computeKillLimitMs(int timeLimitMs, double ratio) {
    int base = ((timeLimitMs + 999) / 1000) * 1000;
    double grace = std::max(2000.0, 2.0 * (double)timeLimitMs) * ratio;
    return base + (int)std::ceil(grace);
}

// 测试点生成器: 评测前运行 generator <测试点编号>, 生成 data.in/data.out 作为该测试点数据
constexpr int GENERATOR_TIME_LIMIT_MS = 10000;   // 生成器墙钟时限 (超时该点判 SYSTEM_ERROR)
constexpr int GENERATOR_MEMORY_LIMIT_MB = 1024;  // 生成器内存上限 (出题人代码, 给足余量)

inline std::string trimTail(const std::string& s, size_t limit) {
    if (s.size() <= limit) return s;
    return "... (truncated)\n" + s.substr(s.size() - limit);
}

// 读取沙箱运行结果 → RunOutcome (文件 stdio 与管道 stdio 共用)
// outFile 可为空 (管道模式下 stdout 未捕获)
inline RunOutcome readRunOutcome(const std::string& metaFile,
                                 const std::string& outFile,
                                 const std::string& errFile,
                                 int timeLimitMs) {
    RunOutcome out;
    out.status = JudgeStatus::SYSTEM_ERROR;
    if (!outFile.empty()) out.output = readFileContent(outFile);
    out.error = readFileContent(errFile);

    json meta = readMetaFile(metaFile);
    if (meta.is_null()) {
        out.status = JudgeStatus::SYSTEM_ERROR;
        out.message = "System Error: no sandbox metadata";
        return out;
    }
    out.exitCode = meta.value("exit_code", 0);
    out.timeUsedMs = meta.value("time_used", 0);
    out.memoryUsedKB = meta.value("memory_used", 0);
    std::string signal = meta.value("signal", "null");

    if (signal == "MEMORY_LIMIT") {
        out.status = JudgeStatus::MEMORY_LIMIT_EXCEEDED;
        out.message = "Memory Limit Exceeded";
    } else if (signal == "PROC_LIMIT") {
        // 进程数超限 (沙箱轮询杀进程组), 不是超时 — 原因是选手代码 fork 了子进程
        out.status = JudgeStatus::RUNTIME_ERROR;
        out.message = "Runtime Error: process limit exceeded";
        if (!out.error.empty()) out.message += "\n" + trimTail(out.error, 1000);
    } else if (signal == "SIGKILL") {
        out.status = JudgeStatus::TIME_LIMIT_EXCEEDED;
        out.message = "Time Limit Exceeded";
    } else if (signal == "OUTPUT_LIMIT") {
        out.status = JudgeStatus::OUTPUT_LIMIT_EXCEEDED;
        out.message = "Output Limit Exceeded";
    } else if (signal == "SYSTEM_ERROR") {
        out.status = JudgeStatus::SYSTEM_ERROR;
        out.message = "System Error";
    } else if (out.exitCode != 0) {
        out.status = JudgeStatus::RUNTIME_ERROR;
        out.message = "Runtime Error (exit code: " + std::to_string(out.exitCode) + ")";
        if (!out.error.empty()) out.message += "\n" + trimTail(out.error, 1000);
    } else if (timeLimitMs > 0 && out.timeUsedMs > timeLimitMs) {
        // 正常退出但墙钟超过时限 (宽限窗口内的边界情况由上层重跑)
        out.status = JudgeStatus::TIME_LIMIT_EXCEEDED;
        out.message = "Time Limit Exceeded";
    } else {
        out.status = JudgeStatus::ACCEPTED;
        out.message = "OK";
    }
    return out;
}

// 运行可执行程序 (文件 stdio, 与其它评测线程隔离)
// ioDir: stdin/stdout/stderr/meta 文件目录; cwd: 子进程工作目录 (空 = ioDir)
// trusted: 可信运行 (无低完整性标签/受限令牌) — communication_exec 的 grader
//          需要在工作目录内创建文件并启动选手进程, 故需可信
inline RunOutcome runProgram(const std::string& exePath,
                             const std::vector<std::string>& exeArgs,
                             const std::string& inputData,
                             int timeLimitMs, int memoryLimitMB,
                             const std::string& ioDir,
                             const std::string& cwd,
                             size_t outputLimitBytes,
                             bool trusted = false) {
    fs::create_directories(ioDir);
    std::string inFile = platform::pathJoin(ioDir, "_stdin.txt");
    std::string outFile = platform::pathJoin(ioDir, "_stdout.txt");
    std::string errFile = platform::pathJoin(ioDir, "_stderr.txt");
    std::string metaFile = platform::pathJoin(ioDir, "_meta.json");
    fs::remove(metaFile);
    if (!writeFileContent(inFile, inputData)) {
        RunOutcome fail;
        fail.status = JudgeStatus::SYSTEM_ERROR;
        fail.message = "System Error: cannot write stdin file";
        return fail;
    }

    SandboxStdio io;
    io.inherit = false;
    io.stdinPath = inFile;
    io.stdoutPath = outFile;
    io.stderrPath = errFile;

    int killLimit = computeKillLimitMs(timeLimitMs, settings::getDefaultExtraTimeRatio());
    std::string runCwd = cwd.empty() ? fs::absolute(ioDir).string() : cwd;

    auto sr = clijudge::sandbox_run(
        killLimit,
        memoryLimitMB,
        // 可信 grader (communication_exec) 需要启动选手子进程: 放开进程数上限
        trusted ? 64 : 1,
        metaFile.c_str(),
        exePath.c_str(),
        exeArgs,
        false,
        &io,
        runCwd,
        settings::extraEnvList(),
        outputLimitBytes,
        trusted
    );
    (void)sr;

    return readRunOutcome(metaFile, outFile, errFile, timeLimitMs);
}

// ── Special Judge 运行 ───────────────────────────────────────
inline bool isSpecialJudgeMode(const std::string& mode) {
    return mode == "spj" || mode == "spj_lemon" || mode == "spj_testlib";
}

// testlib 消息 → 判定 (LemonLime testlibSpecialJudge 语义; 不检查退出码)
// text: 检查器 stderr 内容; matched=false 表示没有可识别前缀
struct TestlibVerdict {
    bool matched = false;
    JudgeStatus status = JudgeStatus::WRONG_ANSWER;
    int score = 0;
    std::string message;
};

inline TestlibVerdict parseTestlibVerdict(const std::string& text, int fullScore) {
    TestlibVerdict v;
    std::string line = text;
    size_t nl = line.find('\n');
    if (nl != std::string::npos) line = line.substr(0, nl);
    line = trimLineEnd(line);
    auto startsWith = [&](const char* prefix) { return text.rfind(prefix, 0) == 0; };

    if (startsWith("FAIL")) {
        v.matched = true;
        v.status = JudgeStatus::INVALID_SPJ;
        v.message = "Invalid Special Judge: " + line;
        return v;
    }
    if (startsWith("ok")) {
        v.matched = true;
        v.status = JudgeStatus::ACCEPTED;
        v.score = fullScore;
        v.message = line.empty() ? "Correct" : line;
        return v;
    }
    const std::string pcPrefix = "partially correct (";
    if (startsWith(pcPrefix.c_str())) {
        size_t b = pcPrefix.size();
        size_t e = text.find(')', b);
        if (e != std::string::npos) {
            std::string numStr = text.substr(b, e - b);
            char* end = nullptr;
            double n = std::strtod(numStr.c_str(), &end);
            if (end != numStr.c_str() && end && *end == '\0') {
                v.matched = true;
                double score = n * (double)fullScore / 100.0;
                if (score < 0) {
                    v.status = JudgeStatus::INVALID_SPJ;
                    v.message = "Invalid Special Judge: negative score";
                } else if (score <= 0.0) {
                    v.status = JudgeStatus::WRONG_ANSWER;
                    v.message = line;
                } else if (score >= (double)fullScore) {
                    v.status = JudgeStatus::ACCEPTED;
                    v.score = fullScore;
                    v.message = line.empty() ? "Correct" : line;
                } else {
                    v.status = JudgeStatus::PARTIALLY_CORRECT;
                    v.score = (int)std::lround(score);
                    v.message = line;
                }
                return v;
            }
        }
    }
    const std::string ptPrefix = "points ";
    if (startsWith(ptPrefix.c_str())) {
        std::string numStr = text.substr(ptPrefix.size());
        size_t k = 0;
        while (k < numStr.size() && (std::isdigit((unsigned char)numStr[k]) || numStr[k] == '.'))
            k++;
        numStr = numStr.substr(0, k);
        char* end = nullptr;
        double x = numStr.empty() ? 0.0 : std::strtod(numStr.c_str(), &end);
        if (!numStr.empty() && end != numStr.c_str()) {
            v.matched = true;
            double score = x * (double)fullScore;
            if (score < 0) {
                v.status = JudgeStatus::INVALID_SPJ;
                v.message = "Invalid Special Judge: negative score";
            } else if (score <= 0.0) {
                v.status = JudgeStatus::WRONG_ANSWER;
                v.message = line;
            } else if (score >= (double)fullScore) {
                v.status = JudgeStatus::ACCEPTED;
                v.score = fullScore;
                v.message = line.empty() ? "Correct" : line;
            } else {
                v.status = JudgeStatus::PARTIALLY_CORRECT;
                v.score = (int)std::lround(score);
                v.message = line;
            }
            return v;
        }
    }
    if (startsWith("wrong")) {
        v.matched = true;
        v.status = JudgeStatus::WRONG_ANSWER;
        v.message = line.empty() ? "Wrong Answer" : line;
        return v;
    }
    v.matched = false;
    v.status = JudgeStatus::WRONG_ANSWER;
    v.score = 0;
    v.message = line;
    return v;
}

inline CompareResult runSpecialJudge(const std::string& spjExe,
                                     const std::string& mode,
                                     const std::string& inputPath,
                                     const std::string& actualPath,
                                     const std::string& expectedPath,
                                     int fullScore,
                                     const std::string& workDir) {
    fs::create_directories(workDir);
    std::string scoreFile = platform::pathJoin(workDir, "_score.txt");
    std::string msgFile = platform::pathJoin(workDir, "_message.txt");
    std::string outFile = platform::pathJoin(workDir, "_spj_stdout.txt");
    std::string errFile = platform::pathJoin(workDir, "_spj_stderr.txt");
    std::string inDummy = platform::pathJoin(workDir, "_spj_stdin.txt");
    std::string metaFile = platform::pathJoin(workDir, "_spj_meta.json");
    fs::remove(metaFile);
    fs::remove(scoreFile);
    fs::remove(msgFile);
    if (!writeFileContent(inDummy, ""))
        return mkCompare(JudgeStatus::SYSTEM_ERROR, 0,
                         "System Error: cannot write special judge stdin");

    std::vector<std::string> args;
    if (mode == "spj_lemon") {
        args = {inputPath, actualPath, expectedPath,
                std::to_string(fullScore), scoreFile, msgFile};
    } else {
        // spj / spj_testlib: (input, 选手输出, 标准输出)
        args = {inputPath, actualPath, expectedPath};
    }

    SandboxStdio io;
    io.inherit = false;
    io.stdinPath = inDummy;
    io.stdoutPath = outFile;
    io.stderrPath = errFile;

    auto sr = clijudge::sandbox_run(
        settings::getSpecialJudgeTimeLimit(),
        512,
        1,
        metaFile.c_str(),
        spjExe.c_str(),
        args,
        false,
        &io,
        workDir,
        settings::extraEnvList(),
        settings::getFileWriteLimitBytes(),
        true   // SPJ 需要在工作目录写 score/message 文件: 可信运行
    );
    (void)sr;

    json meta = readMetaFile(metaFile);
    if (meta.is_null())
        return mkCompare(JudgeStatus::SYSTEM_ERROR, 0, "System Error: special judge did not run");
    std::string signal = meta.value("signal", "null");
    int exitCode = meta.value("exit_code", 0);
    std::string stderrText = readFileContent(errFile);

    if (signal == "SIGKILL")
        return mkCompare(JudgeStatus::SPECIAL_JUDGE_TLE, 0, "Special Judge Time Limit Exceeded");
    if (signal == "PROC_LIMIT")
        return mkCompare(JudgeStatus::SPECIAL_JUDGE_RE, 0,
                         "Special Judge Runtime Error (process limit exceeded)");
    if (signal == "MEMORY_LIMIT" || signal == "OUTPUT_LIMIT")
        return mkCompare(JudgeStatus::SPECIAL_JUDGE_RE, 0, "Special Judge Runtime Error");
    if (signal == "SYSTEM_ERROR")
        return mkCompare(JudgeStatus::SYSTEM_ERROR, 0, "System Error: special judge");

    if (mode == "spj_lemon") {
        if (exitCode != 0)
            return mkCompare(JudgeStatus::SPECIAL_JUDGE_RE, 0,
                             "Special Judge Runtime Error (exit code: " + std::to_string(exitCode) + ")");
        std::string scoreText = trimLineEnd(readFileContent(scoreFile));
        if (scoreText.empty())
            return mkCompare(JudgeStatus::INVALID_SPJ, 0, "Invalid Special Judge: empty score file");
        char* end = nullptr;
        double score = std::strtod(scoreText.c_str(), &end);
        if (end == scoreText.c_str() || (end && *end != '\0'))
            return mkCompare(JudgeStatus::INVALID_SPJ, 0, "Invalid Special Judge: unreadable score");
        if (score < 0)
            return mkCompare(JudgeStatus::INVALID_SPJ, 0, "Invalid Special Judge: negative score");
        std::string msg = trimLineEnd(readFileContent(msgFile));
        if (score >= (double)fullScore)
            return mkCompare(JudgeStatus::ACCEPTED, fullScore, msg.empty() ? "Correct" : msg);
        if (score <= 0.0)
            return mkCompare(JudgeStatus::WRONG_ANSWER, 0, msg.empty() ? "Wrong Answer" : msg);
        return mkCompare(JudgeStatus::PARTIALLY_CORRECT, (int)std::lround(score),
                         msg.empty() ? "Partially Correct" : msg);
    }

    if (mode == "spj_testlib") {
        // 不检查退出码, 按 testlib 前缀解析 stderr
        TestlibVerdict v = parseTestlibVerdict(stderrText, fullScore);
        return mkCompare(v.status, v.score,
                         v.message.empty() ? "Wrong Answer" : v.message);
    }

    // 传统 spj: exit 0 → AC, 否则 WA (保持 CliJudge 原有语义)
    if (exitCode == 0)
        return mkCompare(JudgeStatus::ACCEPTED, fullScore, "Correct");
    return mkCompare(JudgeStatus::WRONG_ANSWER, 0,
                     "Wrong Answer (special judge exit code: " + std::to_string(exitCode) + ")");
}

// ── 单个测试点评判 ───────────────────────────────────────────
struct TestCaseContext {
    std::string exePath;
    std::vector<std::string> exeArgs;
    int timeLimitMs = 1000;
    int memoryLimitMB = 256;
    std::string compareMode = "text_strict";
    double floatAbsTol = 0.0;
    double floatRelTol = 0.0;
    std::string spjExe;      // spj* 模式下已编译的 SPJ
    int rejudgeTimes = 0;    // 边界 TLE 自动重跑次数
    double extraRatio = 0.1;
    std::string runCwd;      // 子进程工作目录 (空 = ioDir)
    bool trustedRun = false; // 可信运行 (communication_exec grader)
};

// ioDir: 选手沙箱工作目录 (含 _stdin/_stdout/_stderr/_meta, 是选手 cwd)
// dataDir: 判题器专用目录 (baseDir/d_i, 非选手 cwd) — 测试数据文件只写这里
inline TestCaseResult judgeTestCase(int tcId, int maxScore,
                                    const std::string& inputData,
                                    const std::string& expectedOutput,
                                    const std::string& ioDir,
                                    const std::string& dataDir,
                                    const TestCaseContext& ctx) {
    TestCaseResult result;
    result.id = tcId;
    result.score = 0;
    result.maxScore = maxScore;
    result.status = JudgeStatus::SYSTEM_ERROR;
    result.timeUsedMs = 0;
    result.memoryUsedKB = 0;

    fs::create_directories(ioDir);

    // 选手输出文件 (SPJ 运行时按绝对路径读取)
    std::string actualPath = platform::pathJoin(ioDir, "_stdout.txt");

    // 裸命令名 (python3/java/node) 无路径分隔符, 由执行层 PATH 解析, 此处无法 exists
    bool hasPathSep = ctx.exePath.find_first_of("/\\") != std::string::npos;
    if (ctx.exePath.empty() || (hasPathSep && !fs::exists(ctx.exePath))) {
        result.status = JudgeStatus::COMPILATION_ERROR;
        result.message = "Executable not found: " + ctx.exePath;
        return result;
    }

    size_t outputLimit = settings::getFileWriteLimitBytes();
    RunOutcome out = runProgram(ctx.exePath, ctx.exeArgs, inputData,
                                ctx.timeLimitMs, ctx.memoryLimitMB,
                                ioDir, ctx.runCwd, outputLimit, ctx.trustedRun);

    // 边界 TLE 自动重跑 (LemonLime extraTime/rejudge 语义)
    int attempts = 0;
    auto withinGrace = [&](const RunOutcome& o) {
        if (o.status != JudgeStatus::TIME_LIMIT_EXCEEDED) return false;
        double limit = (double)ctx.timeLimitMs * (1.0 + ctx.extraRatio) +
                       1000.0 * ctx.extraRatio;
        return (double)o.timeUsedMs <= limit;
    };
    while (withinGrace(out) && attempts < ctx.rejudgeTimes) {
        attempts++;
        out = runProgram(ctx.exePath, ctx.exeArgs, inputData,
                         ctx.timeLimitMs, ctx.memoryLimitMB,
                         ioDir, ctx.runCwd, outputLimit, ctx.trustedRun);
    }

    result.timeUsedMs = out.timeUsedMs;
    result.memoryUsedKB = out.memoryUsedKB;

    if (out.status != JudgeStatus::ACCEPTED) {
        result.status = out.status;
        result.message = out.message;
        return result;
    }

    // 比较 / Special Judge
    // SPJ 才需要标准答案落盘: 且在选手进程结束后才写入 dataDir (非选手 cwd),
    // 选手运行期间磁盘上不存在 test_expected.txt — 杜绝读答案作弊
    CompareResult cr;
    if (isSpecialJudgeMode(ctx.compareMode)) {
        fs::create_directories(dataDir);
        std::string inputPath = platform::pathJoin(dataDir, "test_input.txt");
        std::string expectedPath = platform::pathJoin(dataDir, "test_expected.txt");
        if (!writeFileContent(inputPath, inputData) ||
            !writeFileContent(expectedPath, expectedOutput)) {
            result.status = JudgeStatus::SYSTEM_ERROR;
            result.message = "System Error: cannot write test data files";
            return result;
        }
        cr = runSpecialJudge(ctx.spjExe, ctx.compareMode,
                             inputPath, actualPath, expectedPath,
                             maxScore, dataDir);
    } else {
        cr = compareOutputs(ctx.compareMode, expectedOutput, out.output,
                            maxScore, ctx.floatAbsTol, ctx.floatRelTol);
    }
    result.status = cr.status;
    result.score = cr.score < 0 ? 0 : (cr.score > maxScore ? maxScore : cr.score);
    result.message = cr.message;
    if (result.status == JudgeStatus::ACCEPTED) result.score = maxScore;
    if (result.status != JudgeStatus::ACCEPTED && result.status != JudgeStatus::PARTIALLY_CORRECT)
        result.score = 0;
    return result;
}

// ── 答题模式 (answers_only) ──────────────────────────────────
// 不编译不运行; 作答文件 <base>.<answerExt> 与标准答案比较 (含 SPJ)
inline TestCaseResult judgeAnswersOnly(int tcId, int maxScore, const json& tc,
                                       const std::string& answersDir,
                                       const std::string& answerExt,
                                       const std::string& compareMode,
                                       double absTol, double relTol,
                                       const std::string& spjExe,
                                       const std::string& ioDir) {
    TestCaseResult r;
    r.id = tcId;
    r.score = 0;
    r.maxScore = maxScore;
    r.status = JudgeStatus::SYSTEM_ERROR;
    r.timeUsedMs = 0;
    r.memoryUsedKB = 0;

    fs::create_directories(ioDir);
    std::string ext = answerExt.empty() ? "out" : answerExt;
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::string inputFile = tc.value("input_file", "");
    std::string base = inputFile.empty() ? std::to_string(tcId)
                                         : fs::path(inputFile).stem().string();

    std::string answerFile;
    std::string cand = platform::pathJoin(answersDir, base + "." + ext);
    if (fs::exists(cand)) {
        answerFile = cand;
    } else {
        // 支持作答包内一层子目录 (LemonLime <contestant>/<name>.<ext>)
        std::error_code ec;
        for (auto it = fs::directory_iterator(answersDir, ec);
             !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (!it->is_directory()) continue;
            std::string c2 = platform::pathJoin(it->path().string(), base + "." + ext);
            if (fs::exists(c2)) {
                answerFile = c2;
                break;
            }
        }
    }
    if (answerFile.empty()) {
        r.status = JudgeStatus::WRONG_ANSWER;
        r.message = "Answer file not found: " + base + "." + ext;
        return r;
    }

    std::string expected = tc.value("output_data", "");
    std::string inputPath = platform::pathJoin(ioDir, "test_input.txt");
    std::string expectedPath = platform::pathJoin(ioDir, "test_expected.txt");
    if (!writeFileContent(inputPath, tc.value("input_data", "")) ||
        !writeFileContent(expectedPath, expected)) {
        r.message = "System Error: cannot write test data files";
        return r;
    }

    CompareResult cr;
    if (isSpecialJudgeMode(compareMode)) {
        cr = runSpecialJudge(spjExe, compareMode, inputPath, answerFile, expectedPath,
                             maxScore, ioDir);
    } else {
        cr = compareOutputs(compareMode, expected, readFileContent(answerFile),
                            maxScore, absTol, relTol);
    }
    r.status = cr.status;
    r.score = cr.score < 0 ? 0 : (cr.score > maxScore ? maxScore : cr.score);
    if (r.status == JudgeStatus::ACCEPTED) r.score = maxScore;
    if (r.status != JudgeStatus::ACCEPTED && r.status != JudgeStatus::PARTIALLY_CORRECT)
        r.score = 0;
    r.message = cr.message;
    return r;
}

// ── 双进程管道评测 (interaction / communication_exec) ────────
// contestant stdio ←→ grader stdio 双向管道; 两侧各自沙箱运行。
// 父进程在一方结束后立刻关闭自己持有的该方管道端副本, 使对端读到 EOF。
struct DualOutcome {
    RunOutcome contestant;
    JudgeStatus graderMetaStatus = JudgeStatus::ACCEPTED; // 沙箱级 grader 状态
    int graderExitCode = 0;
    int graderTimeUsedMs = 0;
    std::string graderStderr;
};

inline DualOutcome runDualProcess(const std::string& contestantExe,
                                  const std::vector<std::string>& contestantArgs,
                                  const std::string& graderExe,
                                  const std::vector<std::string>& graderArgs,
                                  int timeLimitMs, int memoryLimitMB,
                                  int graderTimeLimitMs,
                                  const std::string& ioDir) {
    DualOutcome d;
    fs::create_directories(ioDir);
    std::string cErr = platform::pathJoin(ioDir, "_stderr.txt");
    std::string gErr = platform::pathJoin(ioDir, "_grader_stderr.txt");
    std::string cMeta = platform::pathJoin(ioDir, "_meta.json");
    std::string gMeta = platform::pathJoin(ioDir, "_grader_meta.json");
    fs::remove(cMeta);
    fs::remove(gMeta);

    size_t outputLimit = settings::getFileWriteLimitBytes();
    std::string runCwd = fs::absolute(ioDir).string();
    int cKill = computeKillLimitMs(timeLimitMs, settings::getDefaultExtraTimeRatio());
    int gKill = computeKillLimitMs(graderTimeLimitMs, settings::getDefaultExtraTimeRatio());

    SandboxStdio cIo;
    cIo.inherit = false;
    cIo.stderrPath = cErr;
    SandboxStdio gIo;
    gIo.inherit = false;
    gIo.stderrPath = gErr;

#ifdef _WIN32
    SECURITY_ATTRIBUTES saN = {};
    saN.nLength = sizeof(saN);
    saN.bInheritHandle = FALSE;  // 只在各自 spawn 窗口内临时开启所需端
    HANDLE c2iR = NULL, c2iW = NULL, i2cR = NULL, i2cW = NULL;
    if (!CreatePipe(&c2iR, &c2iW, &saN, 0)) {
        d.contestant.status = JudgeStatus::SYSTEM_ERROR;
        d.contestant.message = "System Error: pipe creation failed";
        d.graderMetaStatus = JudgeStatus::SYSTEM_ERROR;
        return d;
    }
    if (!CreatePipe(&i2cR, &i2cW, &saN, 0)) {
        // 第二个管道失败: 先关掉第一个, 避免泄漏 (原 || 短路直接 return)
        CloseHandle(c2iR); CloseHandle(c2iW); c2iR = NULL; c2iW = NULL;
        d.contestant.status = JudgeStatus::SYSTEM_ERROR;
        d.contestant.message = "System Error: pipe creation failed";
        d.graderMetaStatus = JudgeStatus::SYSTEM_ERROR;
        return d;
    }
    cIo.hStdin = i2cR;
    cIo.hStdout = c2iW;
    gIo.hStdin = c2iR;
    gIo.hStdout = i2cW;
    auto closeC = [&]() {
        if (c2iW) { CloseHandle(c2iW); c2iW = NULL; }
        if (i2cR) { CloseHandle(i2cR); i2cR = NULL; }
    };
    auto closeG = [&]() {
        if (c2iR) { CloseHandle(c2iR); c2iR = NULL; }
        if (i2cW) { CloseHandle(i2cW); i2cW = NULL; }
    };
    auto closeAll = [&]() {
        closeC();
        closeG();
    };
#else
    int c2i[2] = { -1, -1 }, i2c[2] = { -1, -1 };
    if (pipe(c2i) != 0) {
        d.contestant.status = JudgeStatus::SYSTEM_ERROR;
        d.contestant.message = "System Error: pipe creation failed";
        d.graderMetaStatus = JudgeStatus::SYSTEM_ERROR;
        return d;
    }
    if (pipe(i2c) != 0) {
        // 第二个管道失败: 先关掉第一个, 避免泄漏 (原 || 短路直接 return)
        if (c2i[0] >= 0) ::close(c2i[0]);
        if (c2i[1] >= 0) ::close(c2i[1]);
        c2i[0] = c2i[1] = -1;
        d.contestant.status = JudgeStatus::SYSTEM_ERROR;
        d.contestant.message = "System Error: pipe creation failed";
        d.graderMetaStatus = JudgeStatus::SYSTEM_ERROR;
        return d;
    }
    cIo.fdStdin = i2c[0];
    cIo.fdStdout = c2i[1];
    gIo.fdStdin = c2i[0];
    gIo.fdStdout = i2c[1];
    auto closeC = [&]() {
        if (c2i[1] >= 0) { ::close(c2i[1]); c2i[1] = -1; }
        if (i2c[0] >= 0) { ::close(i2c[0]); i2c[0] = -1; }
    };
    auto closeG = [&]() {
        if (c2i[0] >= 0) { ::close(c2i[0]); c2i[0] = -1; }
        if (i2c[1] >= 0) { ::close(i2c[1]); i2c[1] = -1; }
    };
    auto closeAll = [&]() {
        closeC();
        closeG();
    };
#endif

    auto futC = std::async(std::launch::async, [&]() {
        return clijudge::sandbox_run(
            cKill, memoryLimitMB, 1, cMeta.c_str(), contestantExe.c_str(),
            contestantArgs, false, &cIo, runCwd, settings::extraEnvList(), outputLimit);
    });
    auto futG = std::async(std::launch::async, [&]() {
        return clijudge::sandbox_run(
            gKill, 512, 1, gMeta.c_str(), graderExe.c_str(),
            graderArgs, false, &gIo, runCwd, settings::extraEnvList(),
            settings::getFileWriteLimitBytes(),
            true);  // grader/interactor 需要写结果文件: 可信运行
    });

    // 轮询: 一方结束后立刻关闭其父进程端副本 (否则对端等不到 EOF)
    bool cDone = false, gDone = false;
    while (!cDone || !gDone) {
        if (!cDone && futC.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            cDone = true;
            closeC();
        }
        if (!gDone && futG.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            gDone = true;
            closeG();
        }
        if (!cDone || !gDone)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    (void)futC.get();
    (void)futG.get();
    closeAll();

    d.contestant = readRunOutcome(cMeta, "", cErr, timeLimitMs);

    json gm = readMetaFile(gMeta);
    if (gm.is_null()) {
        d.graderMetaStatus = JudgeStatus::SYSTEM_ERROR;
    } else {
        std::string gSignal = gm.value("signal", "null");
        d.graderExitCode = gm.value("exit_code", 0);
        d.graderTimeUsedMs = gm.value("time_used", 0);
        if (gSignal == "SIGKILL") d.graderMetaStatus = JudgeStatus::SPECIAL_JUDGE_TLE;
        else if (gSignal == "PROC_LIMIT")
            d.graderMetaStatus = JudgeStatus::SPECIAL_JUDGE_RE;
        else if (gSignal == "MEMORY_LIMIT" || gSignal == "OUTPUT_LIMIT")
            d.graderMetaStatus = JudgeStatus::SPECIAL_JUDGE_RE;
        else if (gSignal == "SYSTEM_ERROR") d.graderMetaStatus = JudgeStatus::SYSTEM_ERROR;
        else d.graderMetaStatus = JudgeStatus::ACCEPTED;
    }
    d.graderStderr = readFileContent(gErr);
    return d;
}

// 单个测试点: 双进程评测 + 判定 (interaction: grader 参数 = input result;
// communication_exec: grader 无参数, 由其自身逻辑读取工作目录文件)
inline TestCaseResult judgeDualTest(int tcId, int maxScore, const json& tc,
                                     const std::string& contestantExe,
                                     const std::vector<std::string>& contestantArgs,
                                     const std::string& graderExe,
                                     bool interactorStyle,
                                     int timeLimitMs, int memoryLimitMB,
                                     const std::string& ioDir,
                                     const std::string& dataDir) {
    TestCaseResult r;
    r.id = tcId;
    r.score = 0;
    r.maxScore = maxScore;
    r.status = JudgeStatus::SYSTEM_ERROR;
    r.timeUsedMs = 0;
    r.memoryUsedKB = 0;

    fs::create_directories(ioDir);
    fs::create_directories(dataDir);
    // 交互输入写入 dataDir (非选手 cwd): 选手无法从工作目录取用完整输入
    std::string inputPath = platform::pathJoin(dataDir, "test_input.txt");
    if (!writeFileContent(inputPath, tc.value("input_data", ""))) {
        r.message = "System Error: cannot write test data file";
        return r;
    }

    std::vector<std::string> graderArgs;
    if (interactorStyle) {
        std::string resultPath = platform::pathJoin(dataDir, "_grader_result.txt");
        graderArgs = { inputPath, resultPath };
    }

    DualOutcome d = runDualProcess(contestantExe, contestantArgs,
                                   graderExe, graderArgs,
                                   timeLimitMs, memoryLimitMB,
                                   settings::getSpecialJudgeTimeLimit(), ioDir);

    r.timeUsedMs = d.contestant.timeUsedMs;
    r.memoryUsedKB = d.contestant.memoryUsedKB;

    // grader/interactor 沙箱级故障优先
    if (d.graderMetaStatus == JudgeStatus::SYSTEM_ERROR) {
        r.status = JudgeStatus::SYSTEM_ERROR;
        r.message = "System Error: interactor";
        return r;
    }
    if (d.graderMetaStatus == JudgeStatus::SPECIAL_JUDGE_TLE) {
        r.status = JudgeStatus::SPECIAL_JUDGE_TLE;
        r.message = "Interactor Time Limit Exceeded";
        return r;
    }
    if (d.graderMetaStatus == JudgeStatus::SPECIAL_JUDGE_RE) {
        r.status = JudgeStatus::SPECIAL_JUDGE_RE;
        r.message = "Interactor Runtime Error";
        return r;
    }

    // 选手侧异常 (TLE/MLE/RE/OLE) 优先于 grader 判定
    const RunOutcome& c = d.contestant;
    if (c.status != JudgeStatus::ACCEPTED) {
        r.status = c.status;
        r.message = c.message;
        return r;
    }

    // grader stderr testlib 风格判定, 无匹配时按退出码回退
    TestlibVerdict v = parseTestlibVerdict(d.graderStderr, maxScore);
    if (v.matched) {
        r.status = v.status;
        r.score = v.score;
        if (r.status == JudgeStatus::ACCEPTED) r.score = maxScore;
        if (r.status != JudgeStatus::ACCEPTED && r.status != JudgeStatus::PARTIALLY_CORRECT)
            r.score = 0;
        if (r.score < 0) r.score = 0;
        if (r.score > maxScore) r.score = maxScore;
        r.message = v.message.empty() ? statusToString(v.status) : v.message;
        return r;
    }
    switch (d.graderExitCode) {
        case 0: r.status = JudgeStatus::ACCEPTED; r.score = maxScore;
                r.message = "Correct"; break;
        case 1: r.status = JudgeStatus::WRONG_ANSWER; r.message = "Wrong Answer"; break;
        case 2: r.status = JudgeStatus::PRESENTATION_ERROR; r.message = "Presentation Error"; break;
        case 3: r.status = JudgeStatus::INTERACTOR_ERROR; r.message = "Interactor Error"; break;
        default:
            r.status = JudgeStatus::INTERACTOR_ERROR;
            r.message = "Interactor Error (exit code: " + std::to_string(d.graderExitCode) + ")";
            if (!d.graderStderr.empty()) r.message += "\n" + trimTail(d.graderStderr, 500);
            break;
    }
    return r;
}

// ── 完整评判 ─────────────────────────────────────────────────
inline JudgeResult judgeSubmission(
    const json& problem,
    const std::string& filePath,
    const std::string& compareMode = "",
    double floatAbsTol = 0.0,
    double floatRelTol = 0.0,
    const std::string& spjExe = ""
) {
    JudgeResult result;
    result.totalScore = 0;
    result.maxScore = 0;
    result.status = JudgeStatus::ACCEPTED;
    result.compileError = "";
    result.totalTimeMs = 0;
    result.maxMemoryKB = 0;

    if (!problem.contains("problem") || !problem["problem"].is_object()) {
        result.status = JudgeStatus::SYSTEM_ERROR;
        result.compileError = "Invalid problem data";
        return result;
    }
    const json& p = problem["problem"];

    // 题目配置
    std::string mode = compareMode.empty() ?
        p.value("compare_mode", "text_strict") : compareMode;
    double absTol = floatAbsTol != 0.0 ? floatAbsTol : p.value("float_abs_tolerance", 0.0);
    double relTol = floatRelTol != 0.0 ? floatRelTol : p.value("float_rel_tolerance", 0.0);
    std::string spjPath = spjExe.empty() ? p.value("special_judge_exe", "") : spjExe;
    std::string subtaskMode = p.value("subtask_mode", "simple");
    std::string pType = p.value("problem_type", "traditional");
    if (pType.empty()) pType = "traditional";
    int defaultTimeLimit = p.value("time_limit", 1000);
    int defaultMemoryLimit = p.value("memory_limit", 256);

    // 进程内首次访问评测设置 (线程池启动前完成静态初始化)
    settings::get();

    // 测试用例
    json testCases = problem.value("test_cases", json::array());
    if (testCases.empty()) {
        result.status = JudgeStatus::SYSTEM_ERROR;
        result.compileError = "No test cases";
        return result;
    }
    std::vector<json> sortedCases(testCases.begin(), testCases.end());
    std::sort(sortedCases.begin(), sortedCases.end(), [](const json& a, const json& b) {
        return a.value("sort_order", 0) < b.value("sort_order", 0);
    });

    // 临时工作目录
    std::string baseDir = platform::pathJoin(
        platform::tempDir(),
        "clijudge_judge_" + std::to_string(platform::pid()) + "_" + std::to_string(platform::tickMs()));
    fs::create_directories(baseDir);

    bool treatAsExecutable;
#ifdef _WIN32
    treatAsExecutable = isExecutable(filePath);
#else
    treatAsExecutable = isElfExecutable(filePath);
#endif

    // 源码大小上限 (LemonLime fileSizeLimit); answers_only 提交物是作答包, 不检查
    if (!treatAsExecutable && pType != "answers_only" && fs::exists(filePath)) {
        std::error_code ec;
        auto sz = fs::file_size(filePath, ec);
        if (!ec && sz > (size_t)settings::getFileSizeLimitKB() * 1024) {
            result.status = JudgeStatus::COMPILATION_ERROR;
            result.compileError = "Source file too large (limit " +
                                  std::to_string(settings::getFileSizeLimitKB()) + " KB)";
            try { fs::remove_all(baseDir); } catch (...) {}
            return result;
        }
    }

    // 按题型准备编译产物
    CompileResult cr;
    std::string graderExe;          // interaction 交互器 / communication_exec grader
    bool interactorReady = false;   // interaction 有交互器可跑双进程管道
    bool graderReady = false;       // communication_exec grader 已编译
    std::string answersDir;         // answers_only 作答目录
    std::string answerExt = p.value("answer_file_extension", "out");

    if (pType == "answers_only") {
        // 不编译; 提交物为目录或 .zip 作答包
        std::error_code ec;
        if (fs::is_directory(filePath, ec)) {
            answersDir = fs::absolute(filePath).string();
        } else if (fs::exists(filePath)) {
            std::string subExt = fs::path(filePath).extension().string();
            std::transform(subExt.begin(), subExt.end(), subExt.begin(),
                           [](unsigned char ch) { return (char)std::tolower(ch); });
            if (subExt != ".zip") {
                result.status = JudgeStatus::SYSTEM_ERROR;
                result.compileError = "answers_only submission must be a directory or .zip file";
                try { fs::remove_all(baseDir); } catch (...) {}
                return result;
            }
            answersDir = platform::pathJoin(baseDir, "answers");
            fs::create_directories(answersDir);
            if (!extractZipToDir(filePath, answersDir)) {
                result.status = JudgeStatus::SYSTEM_ERROR;
                result.compileError = "Failed to extract answer zip";
                try { fs::remove_all(baseDir); } catch (...) {}
                return result;
            }
        } else {
            result.status = JudgeStatus::SYSTEM_ERROR;
            result.compileError = "answers_only submission must be a directory or .zip file";
            try { fs::remove_all(baseDir); } catch (...) {}
            return result;
        }
        cr.success = true;
    } else if (pType == "communication") {
        // 选手源码 + grader 源码合并编译成单一 exe (LemonLime: 直接 traditional 判定)
        json gf = p.value("grader_files", json::object());
        if (treatAsExecutable || !gf.is_object() || gf.empty()) {
            result.status = JudgeStatus::SYSTEM_ERROR;
            result.compileError = treatAsExecutable
                ? "communication requires a source submission"
                : "grader_files missing";
            try { fs::remove_all(baseDir); } catch (...) {}
            return result;
        }
        std::vector<std::string> sources;
        sources.push_back(fs::absolute(filePath).string());
        for (auto it = gf.begin(); it != gf.end(); ++it) {
            std::string key = it.key();
            std::replace(key.begin(), key.end(), '\\', '/');
            if (key.empty() || key[0] == '/' || key.find(':') != std::string::npos ||
                key.find("..") != std::string::npos) continue;
            if (!it->is_string()) continue;
            std::string outPath = platform::pathJoin(baseDir, key);
            fs::create_directories(fs::path(outPath).parent_path());
            if (!writeFileContent(outPath, it.value().get<std::string>())) {
                result.status = JudgeStatus::SYSTEM_ERROR;
                result.compileError = "System Error: cannot write submission file: " + key;
                try { fs::remove_all(baseDir); } catch (...) {}
                return result;
            }
            sources.push_back(outPath);
        }
        if (sources.size() < 2) {
            result.status = JudgeStatus::SYSTEM_ERROR;
            result.compileError = "grader_files missing";
            try { fs::remove_all(baseDir); } catch (...) {}
            return result;
        }
        std::string exeName = p.value("source_file_name", "");
        if (exeName.empty()) exeName = fs::path(filePath).stem().string();
        cr = compileSources(sources, baseDir, exeName);
    } else if (pType == "communication_exec") {
        // 选手 exe 与 grader 分开编译; grader 为主进程 (LemonLime: executableFile = "grader"),
        // grader 在工作目录内自行启动选手进程, 输出按 traditional 比较
        json gf = p.value("grader_files", json::object());
        if (treatAsExecutable || !gf.is_object() || gf.empty()) {
            result.status = JudgeStatus::SYSTEM_ERROR;
            result.compileError = treatAsExecutable
                ? "communication_exec requires a source submission"
                : "grader_files missing";
            try { fs::remove_all(baseDir); } catch (...) {}
            return result;
        }
        std::string graderMainPath;
        for (auto it = gf.begin(); it != gf.end(); ++it) {
            std::string key = it.key();
            std::replace(key.begin(), key.end(), '\\', '/');
            if (key.empty() || key[0] == '/' || key.find(':') != std::string::npos ||
                key.find("..") != std::string::npos) continue;
            if (!it->is_string()) continue;
            std::string outPath = platform::pathJoin(baseDir, key);
            fs::create_directories(fs::path(outPath).parent_path());
            if (!writeFileContent(outPath, it.value().get<std::string>())) {
                result.status = JudgeStatus::SYSTEM_ERROR;
                result.compileError = "System Error: cannot write submission file: " + key;
                try { fs::remove_all(baseDir); } catch (...) {}
                return result;
            }
            std::string fname = fs::path(key).filename().string();
            // 主 grader: grader.<源码扩展名> (对齐 LemonLime commExecGrader)
            if (graderMainPath.empty() && fname.rfind("grader.", 0) == 0) {
                std::string gext = fs::path(fname).extension().string();
                if (gext == ".c" || gext == ".cc" || gext == ".cpp" || gext == ".cxx")
                    graderMainPath = outPath;
            }
        }
        if (graderMainPath.empty()) {
            result.status = JudgeStatus::COMPILATION_ERROR;
            result.compileError = "Main grader (grader.*) cannot be found";
            try { fs::remove_all(baseDir); } catch (...) {}
            return result;
        }
        // 选手源码 → source_file_name 可执行文件 (grader 以该名相对启动)
        std::string exeName = p.value("source_file_name", "");
        if (exeName.empty()) exeName = fs::path(filePath).stem().string();
        cr = compileSources({filePath}, baseDir, exeName);
        if (cr.success) {
            CompileResult gCr = compileSources({graderMainPath}, baseDir, "grader", true);
            if (!gCr.success) {
                result.status = JudgeStatus::COMPILATION_ERROR;
                result.compileError = "Grader compile failed:\n" + gCr.error;
                try { fs::remove_all(baseDir); } catch (...) {}
                return result;
            }
            graderExe = gCr.exePath;
            graderReady = true;
        }
    } else if (pType == "interaction") {
        if (treatAsExecutable) {
            cr.success = true;
            cr.exePath = fs::absolute(filePath).string();
        } else {
            // Lemon 交互题: task.grader 作为选手侧附带源文件一并编译 (Lemon: sourceFile + __grader.cpp)
            std::vector<std::string> iSources;
            iSources.push_back(fs::absolute(filePath).string());
            json gf = p.value("grader_files", json::object());
            if (gf.is_object()) {
                for (auto it = gf.begin(); it != gf.end(); ++it) {
                    std::string key = it.key();
                    std::replace(key.begin(), key.end(), '\\', '/');
                    if (key.empty() || key[0] == '/' || key.find(':') != std::string::npos ||
                        key.find("..") != std::string::npos) continue;
                    if (!it->is_string()) continue;
                    std::string outPath = platform::pathJoin(baseDir, key);
                    fs::create_directories(fs::path(outPath).parent_path());
                    if (!writeFileContent(outPath, it.value().get<std::string>())) {
                        result.status = JudgeStatus::SYSTEM_ERROR;
                        result.compileError = "System Error: cannot write submission file: " + key;
                        try { fs::remove_all(baseDir); } catch (...) {}
                        return result;
                    }
                    iSources.push_back(outPath);
                }
            }
            cr = compileSources(iSources, baseDir, "program");
        }
        // 交互器: 内嵌源码 (interactor_code) 或外部可执行文件 (interactor_data)
        if (cr.success) {
            std::string iCode = p.value("interactor_code", "");
            std::string iData = p.value("interactor_data", "");
            if (!iCode.empty()) {
                std::string srcPath = platform::pathJoin(baseDir, "interactor.cpp");
                if (!writeFileContent(srcPath, iCode)) {
                    result.status = JudgeStatus::SYSTEM_ERROR;
                    result.compileError = "System Error: cannot write interactor source";
                    try { fs::remove_all(baseDir); } catch (...) {}
                    return result;
                }
                CompileResult iCr = compileSources({srcPath}, baseDir, "interactor");
                if (!iCr.success) {
                    result.status = JudgeStatus::SYSTEM_ERROR;
                    result.compileError = "Interactor compile failed:\n" + iCr.error;
                    try { fs::remove_all(baseDir); } catch (...) {}
                    return result;
                }
                graderExe = iCr.exePath;
                interactorReady = true;
            } else if (!iData.empty()) {
                std::error_code ec;
                if (!fs::exists(iData, ec)) {
                    result.status = JudgeStatus::SYSTEM_ERROR;
                    result.compileError = "interactor_data not found: " + iData;
                    try { fs::remove_all(baseDir); } catch (...) {}
                    return result;
                }
                fs::path dst = fs::path(baseDir) / (std::string("interactor") + platform::exeSuffix());
                fs::copy_file(iData, dst, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    result.status = JudgeStatus::SYSTEM_ERROR;
                    result.compileError = "Failed to copy interactor_data: " + ec.message();
                    try { fs::remove_all(baseDir); } catch (...) {}
                    return result;
                }
#ifndef _WIN32
                fs::permissions(dst, fs::perms::owner_all | fs::perms::group_read |
                                     fs::perms::others_read,
                                fs::perm_options::add, ec);
#endif
                graderExe = fs::absolute(dst).string();
                interactorReady = true;
            }
            // 无交互器定义: 回退 traditional (LemonLime 注释掉的分支语义)
        }
    } else {
        if (treatAsExecutable) {
            cr.success = true;
            cr.exePath = fs::absolute(filePath).string();
        } else {
            cr = compileSources({filePath}, baseDir, "program");
        }
    }
    if (!cr.success) {
        result.status = JudgeStatus::COMPILATION_ERROR;
        result.compileError = cr.error;
        try { fs::remove_all(baseDir); } catch (...) {}
        return result;
    }

    // Special Judge
    if (isSpecialJudgeMode(mode)) {
        bool haveExe = !spjPath.empty() && fs::exists(spjPath);
        if (!haveExe) {
            CompileResult spjCr = compileSpecialJudge(problem, baseDir);
            if (!spjCr.success) {
                result.status = JudgeStatus::SYSTEM_ERROR;
                result.compileError = spjCr.error;
                try { fs::remove_all(baseDir); } catch (...) {}
                return result;
            }
            spjPath = spjCr.exePath;
        } else {
            spjPath = fs::absolute(spjPath).string();
        }
    }

    // 测试点生成器: generator_code (内嵌源码, 评测前编译) 或 generator_exe (外部可执行文件)
    std::string genExe;
    bool generatorReady = false;
    if (pType != "answers_only") {
        std::string gCode = p.value("generator_code", "");
        std::string gExeFile = p.value("generator_exe", "");
        if (!gCode.empty()) {
            std::string genSrc = platform::pathJoin(baseDir, "generator.cpp");
            if (!writeFileContent(genSrc, gCode)) {
                result.status = JudgeStatus::SYSTEM_ERROR;
                result.compileError = "System Error: cannot write generator source";
                try { fs::remove_all(baseDir); } catch (...) {}
                return result;
            }
            CompileResult gCr = compileSources({genSrc}, baseDir, "generator");
            if (!gCr.success) {
                result.status = JudgeStatus::SYSTEM_ERROR;
                result.compileError = "Generator compile failed:\n" + gCr.error;
                try { fs::remove_all(baseDir); } catch (...) {}
                return result;
            }
            genExe = gCr.exePath;
            generatorReady = true;
        } else if (!gExeFile.empty()) {
            std::error_code ec;
            if (!fs::exists(gExeFile, ec)) {
                result.status = JudgeStatus::SYSTEM_ERROR;
                result.compileError = "generator_exe not found: " + gExeFile;
                try { fs::remove_all(baseDir); } catch (...) {}
                return result;
            }
            std::string gExt = fs::path(gExeFile).extension().string();
            fs::path gDst = fs::path(baseDir) /
                            ("generator" + (gExt.empty() ? platform::exeSuffix() : gExt));
            fs::copy_file(gExeFile, gDst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                result.status = JudgeStatus::SYSTEM_ERROR;
                result.compileError = "Failed to copy generator_exe: " + ec.message();
                try { fs::remove_all(baseDir); } catch (...) {}
                return result;
            }
#ifndef _WIN32
            fs::permissions(gDst, fs::perms::owner_all | fs::perms::group_read |
                                 fs::perms::others_read,
                            fs::perm_options::add, ec);
#endif
            genExe = fs::absolute(gDst).string();
            generatorReady = true;
        }
    }

    // 构建测试任务表
    struct FlatTest {
        int subtaskId;
        const json* tc;
        size_t idx;
    };
    std::vector<FlatTest> tasks;
    std::map<int, std::vector<size_t>> groups;  // subtaskId → 任务下标 (有序)
    tasks.reserve(sortedCases.size());
    for (size_t i = 0; i < sortedCases.size(); i++) {
        int sid = sortedCases[i].value("subtask_id", 1);
        tasks.push_back({sid, &sortedCases[i], i});
        groups[sid].push_back(i);
    }

    // 评测上下文 (线程共享只读)
    TestCaseContext ctx;
    ctx.exePath = cr.exePath;
    ctx.exeArgs = cr.exeArgs;
    ctx.compareMode = mode;
    ctx.floatAbsTol = absTol;
    ctx.floatRelTol = relTol;
    ctx.spjExe = spjPath;
    ctx.rejudgeTimes = settings::getRejudgeTimes();
    ctx.extraRatio = settings::getDefaultExtraTimeRatio();

    // 评测线程池 (原子取号, 结果写入独立槽位)
    size_t n = tasks.size();
    std::vector<TestCaseResult> testResults(n);
    std::atomic<size_t> next{0};
    int nThreads = settings::getMaxJudgingThreads();
    if ((size_t)nThreads > n) nThreads = (int)n;
    if (nThreads < 1) nThreads = 1;

    // 评测分发: answers_only / interaction(双进程) / communication_exec(grader 主进程) / traditional
    bool isAnswers = (pType == "answers_only");
    bool isDual = (pType == "interaction" && interactorReady);
    bool isCommExec = (pType == "communication_exec" && graderReady);

    auto worker = [&]() {
        for (;;) {
            size_t i = next.fetch_add(1);
            if (i >= n) break;
            const json& tc = *tasks[i].tc;
            int tcId = tc.value("id", (int)i + 1);
            int tcScore = tc.value("score", 10);
            int tcTimeLimit = tc.value("time_limit", -1);
            int tcMemoryLimit = tc.value("memory_limit", -1);
            TestCaseContext local = ctx;
            local.timeLimitMs = (tcTimeLimit > 0) ? tcTimeLimit : defaultTimeLimit;
            local.memoryLimitMB = (tcMemoryLimit > 0) ? tcMemoryLimit : defaultMemoryLimit;
            std::string ioDir = platform::pathJoin(baseDir, "t_" + std::to_string(i));
            std::string dataDir = platform::pathJoin(baseDir, "d_" + std::to_string(i));

            // 测试点生成器: 评测前运行 generator <测试点号>, 在 dataDir 写 data.in/data.out
            // 作为该测试点输入与标准答案 (interaction 由交互器供输入, answers_only 不使用)
            std::string inData = tc.value("input_data", "");
            std::string outData = tc.value("output_data", "");
            if (generatorReady && !isAnswers && !isDual) {
                fs::create_directories(dataDir);
                RunOutcome g = runProgram(genExe, {std::to_string(tcId)}, "",
                                          GENERATOR_TIME_LIMIT_MS, GENERATOR_MEMORY_LIMIT_MB,
                                          dataDir, "", settings::getFileWriteLimitBytes(),
                                          /*trusted=*/true);  // 生成器在 dataDir 写 data.in/data.out
                std::string genErr;
                if (g.status != JudgeStatus::ACCEPTED) {
                    genErr = "generator failed on test point " + std::to_string(tcId) +
                             ": " + g.message;
                } else {
                    std::string dataIn = platform::pathJoin(dataDir, "data.in");
                    std::string dataOut = platform::pathJoin(dataDir, "data.out");
                    if (!fs::exists(dataIn)) {
                        genErr = "generator did not create data.in (test point " +
                                 std::to_string(tcId) + ")";
                    } else if (!fs::exists(dataOut)) {
                        genErr = "generator did not create data.out (test point " +
                                 std::to_string(tcId) + ")";
                    } else {
                        inData = readFileContent(dataIn);
                        outData = readFileContent(dataOut);
                    }
                    // 生成结果已读入内存, 立即删除 — 选手运行期间磁盘不保留 data.out(答案)
                    { std::error_code ecx; fs::remove(dataIn, ecx); fs::remove(dataOut, ecx); }
                }
                if (!genErr.empty()) {
                    TestCaseResult& r = testResults[i];
                    r.id = tcId;
                    r.score = 0;
                    r.maxScore = tcScore;
                    r.status = JudgeStatus::SYSTEM_ERROR;
                    r.timeUsedMs = 0;
                    r.memoryUsedKB = 0;
                    r.message = "System Error: " + genErr;
                    if (!g.error.empty()) r.message += "\n" + trimTail(g.error, 500);
                    continue;
                }
            }

            if (isAnswers) {
                testResults[i] = judgeAnswersOnly(
                    tcId, tcScore, tc, answersDir, answerExt,
                    local.compareMode, local.floatAbsTol, local.floatRelTol,
                    local.spjExe, ioDir);
            } else if (isDual) {
                testResults[i] = judgeDualTest(
                    tcId, tcScore, tc, cr.exePath, cr.exeArgs, graderExe,
                    /*interactorStyle=*/true,
                    local.timeLimitMs, local.memoryLimitMB, ioDir, dataDir);
            } else if (isCommExec) {
                // grader 与选手 exe 拷入本测试工作目录 (grader 以相对名启动选手)
                fs::create_directories(ioDir);
                std::error_code ec2;
                fs::path gDst = fs::path(ioDir) / fs::path(graderExe).filename();
                fs::path cDst = fs::path(ioDir) / fs::path(cr.exePath).filename();
                fs::copy_file(graderExe, gDst, fs::copy_options::overwrite_existing, ec2);
                if (!ec2)
                    fs::copy_file(cr.exePath, cDst, fs::copy_options::overwrite_existing, ec2);
                if (ec2) {
                    TestCaseResult& r = testResults[i];
                    r.id = tcId;
                    r.score = 0;
                    r.maxScore = tcScore;
                    r.status = JudgeStatus::SYSTEM_ERROR;
                    r.timeUsedMs = 0;
                    r.memoryUsedKB = 0;
                    r.message = "System Error: copy executables failed: " + ec2.message();
                    continue;
                }
#ifndef _WIN32
                std::error_code ec3;
                fs::permissions(gDst, fs::perms::owner_all, fs::perm_options::add, ec3);
                fs::permissions(cDst, fs::perms::owner_all, fs::perm_options::add, ec3);
#endif
                TestCaseContext local2 = local;
                local2.exePath = gDst.string();
                local2.exeArgs.clear();
                local2.trustedRun = true;  // grader 需在工作目录创建文件并启动选手进程
                testResults[i] = judgeTestCase(
                    tcId, tcScore,
                    inData,
                    outData,
                    ioDir, dataDir, local2);
            } else {
                testResults[i] = judgeTestCase(
                    tcId, tcScore,
                    inData,
                    outData,
                    ioDir, dataDir, local);
            }
        }
    };
    {
        std::vector<std::thread> pool;
        pool.reserve((size_t)nThreads);
        for (int t = 0; t < nThreads; t++) pool.emplace_back(worker);
        for (auto& th : pool) th.join();
    }

    // 子任务聚合
    std::map<int, SubtaskResult> subMap;
    for (auto& [sid, idxs] : groups) {
        SubtaskResult st;
        st.id = sid;
        st.score = 0;
        st.maxScore = 0;
        st.status = JudgeStatus::ACCEPTED;
        int minScore = -1;
        for (size_t i : idxs) {
            const TestCaseResult& tc = testResults[i];
            st.maxScore += tc.maxScore;
            st.score += tc.score;
            if (minScore < 0 || tc.score < minScore) minScore = tc.score;
            if (st.status == JudgeStatus::ACCEPTED && tc.status != JudgeStatus::ACCEPTED)
                st.status = tc.status;
            st.testCases.push_back(tc);
            result.totalTimeMs += tc.timeUsedMs;
            if (tc.memoryUsedKB > result.maxMemoryKB) result.maxMemoryKB = tc.memoryUsedKB;
        }
        if (subtaskMode == "all_or_nothing" && minScore >= 0) {
            st.score = minScore;
            // 上限取各测试点满分的最小值 (分数 = 各点分数的最小值, 不会超过最低满分)
            int capMax = -1;
            for (size_t i : idxs) {
                if (capMax < 0 || testResults[i].maxScore < capMax) capMax = testResults[i].maxScore;
            }
            if (capMax >= 0) st.maxScore = capMax;
        }
        subMap[sid] = std::move(st);
    }

    // 依赖子任务: 先按 id 升序展开 (依赖必须指向更小的子任务)
    std::vector<SubtaskResult*> order;
    order.reserve(subMap.size());
    for (auto& [sid, st] : subMap) order.push_back(&st);

    // problem.subtask_dependence: {"<subtaskId>": [depId, ...]}
    json depMap = json::object();
    if (p.contains("subtask_dependence") && p["subtask_dependence"].is_object())
        depMap = p["subtask_dependence"];

    for (SubtaskResult* st : order) {
        auto it = depMap.find(std::to_string(st->id));
        if (it == depMap.end() || !it->is_array()) continue;
        bool skipped = false;
        for (const auto& depIdJson : *it) {
            if (!depIdJson.is_number_integer()) continue;
            int depId = depIdJson.get<int>();
            auto dit = subMap.find(depId);
            if (dit == subMap.end()) continue;  // 配置错误: 忽略未知依赖
            const SubtaskResult& dep = dit->second;
            if (dep.maxScore <= 0) continue;
            if (dep.status == JudgeStatus::SKIPPED || dep.score <= 0) {
                st->score = 0;
                st->status = JudgeStatus::SKIPPED;
                skipped = true;
                break;
            }
            if (dep.score < dep.maxScore) {
                double ratio = (double)dep.score / (double)dep.maxScore;
                int cap = (int)std::lround((double)st->maxScore * ratio);
                if (st->score > cap) {
                    st->score = cap;
                    if (st->status == JudgeStatus::ACCEPTED && cap < st->maxScore)
                        st->status = JudgeStatus::PARTIALLY_CORRECT;
                }
            }
        }
        (void)skipped;
    }

    // 汇总
    result.subtasks.clear();
    result.subtasks.reserve(order.size());
    result.status = JudgeStatus::ACCEPTED;
    for (SubtaskResult* st : order) {
        result.totalScore += st->score;
        result.maxScore += st->maxScore;
        if (result.status == JudgeStatus::ACCEPTED && st->status != JudgeStatus::ACCEPTED)
            result.status = st->status;
        result.subtasks.push_back(std::move(*st));
    }
    if (order.empty()) {
        result.status = JudgeStatus::SYSTEM_ERROR;
        result.compileError = "No test cases";
    }

    // 清理临时工作目录
    try { fs::remove_all(baseDir); } catch (...) {}

    return result;
}

// ── JSON 输出 ────────────────────────────────────────────────
inline json resultToJson(const JudgeResult& result) {
    json j;
    j["total_score"] = result.totalScore;
    j["max_score"] = result.maxScore;
    j["status"] = statusToString(result.status);
    j["status_abbr"] = statusToAbbr(result.status);
    j["total_time_ms"] = result.totalTimeMs;
    j["max_memory_kb"] = result.maxMemoryKB;
    j["compile_error"] = result.compileError;

    json subtasks = json::array();
    for (const auto& st : result.subtasks) {
        json stJson;
        stJson["id"] = st.id;
        stJson["score"] = st.score;
        stJson["max_score"] = st.maxScore;
        stJson["status"] = statusToString(st.status);
        stJson["status_abbr"] = statusToAbbr(st.status);

        json cases = json::array();
        for (const auto& tc : st.testCases) {
            json tcJson;
            tcJson["id"] = tc.id;
            tcJson["score"] = tc.score;
            tcJson["max_score"] = tc.maxScore;
            tcJson["status"] = statusToString(tc.status);
            tcJson["status_abbr"] = statusToAbbr(tc.status);
            tcJson["time_ms"] = tc.timeUsedMs;
            tcJson["memory_kb"] = tc.memoryUsedKB;
            tcJson["message"] = tc.message;
            cases.push_back(tcJson);
        }
        stJson["test_cases"] = cases;
        subtasks.push_back(stJson);
    }
    j["subtasks"] = subtasks;

    return j;
}

// ── 显示评判结果 ─────────────────────────────────────────────
inline void printResult(const JudgeResult& result) {
    std::cout << clijudge::lang::tr("judge.result_header", "=== Judge Result ===") << std::endl;
    std::cout << clijudge::lang::tr("judge.score", "Score") << ": "
              << result.totalScore << " / " << result.maxScore << std::endl;
    std::cout << clijudge::lang::tr("judge.status", "Status") << ": " << statusToAbbr(result.status)
              << " (" << statusToDisplay(result.status) << ")" << std::endl;
    std::cout << clijudge::lang::tr("judge.time", "Time") << ": " << result.totalTimeMs << " ms" << std::endl;
    std::cout << clijudge::lang::tr("judge.memory", "Memory") << ": " << result.maxMemoryKB << " KB" << std::endl;

    if (!result.compileError.empty()) {
        std::cout << clijudge::lang::trf("judge.compile_error_detail", "Compile Error: {0}", {result.compileError}) << std::endl;
    }

    for (const auto& st : result.subtasks) {
        std::cout << clijudge::lang::trf("judge.subtask_header", "\n--- Subtask {0} ---", {std::to_string(st.id)}) << std::endl;
        std::cout << clijudge::lang::tr("judge.score", "Score") << ": "
                  << st.score << " / " << st.maxScore << std::endl;
        std::cout << clijudge::lang::tr("judge.status", "Status") << ": " << statusToAbbr(st.status) << std::endl;

        for (const auto& tc : st.testCases) {
            std::cout << clijudge::lang::trf("judge.test_result",
                                             "  Test {0}: {1} ({2} ms, {3} KB)",
                                             {std::to_string(tc.id), statusToAbbr(tc.status),
                                              std::to_string(tc.timeUsedMs), std::to_string(tc.memoryUsedKB)})
                      << std::endl;
        }
    }
}

} // namespace judge
} // namespace clijudge

#endif // CLIJUDGE_JUDGE_H
