#ifndef CLIJUDGE_CONTEST_H
#define CLIJUDGE_CONTEST_H

// contest.h
// CliJudge 比赛管理子命令
//
// 子命令:
//   create [标题] [开始时间] [结束时间] [题目1] [题目2] - 创建比赛
//   delete [编号] - 删除比赛
//   problem [编号] [题目在比赛中的编号]
//     submit [文件地址] - 提交题目
//     view - 查看题目
//   view [编号] - 查看比赛

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cctype>
#include "json.hpp"
#include "cdf.h"
#include "lang.h"
#include "platform.h"
#include "problem.h"

namespace clijudge {
namespace contest {

using json = nlohmann::json;
namespace fs = std::filesystem;

// 比赛数据结构
struct Contest {
    int id;
    std::string title;
    std::string startTime;
    std::string endTime;
    std::vector<int> problemIds;  // 题目ID列表
    std::string createdAt;
};

// 比赛题目提交记录
struct ContestSubmission {
    int contestId;
    int problemIndex;  // 题目在比赛中的编号（从1开始）
    int problemId;     // 实际题目ID
    std::string filePath;
    std::string submittedAt;
    std::string result;
};

// 数据存储类
class ContestStore {
private:
    std::string dataDir;
    json data;

    void ensureDataDir() {
        fs::create_directories(dataDir + "/contests");
    }

    std::string getContestPath(int id) {
        return dataDir + "/contests/contest_" + std::to_string(id) + ".json";
    }

    void loadIndex() {
        std::string indexPath = dataDir + "/contests/contests.json";
        if (fs::exists(indexPath)) {
            std::ifstream f(indexPath);
            if (f.is_open()) {
                f.seekg(0, std::ios::end);
                if (f.tellg() > 0) {
                    f.seekg(0, std::ios::beg);
                    try {
                        f >> data;
                    } catch (...) {
                        data = json::array();
                    }
                }
            }
        }
        if (data.empty()) {
            data = json::array();
        }
    }

    void saveIndex() {
        ensureDataDir();
        std::string indexPath = dataDir + "/contests/contests.json";
        if (!platform::writeFileAtomic(indexPath, data.dump(2))) {
            std::cerr << clijudge::lang::trf("contest.write_failed", "Warning: failed to write {0}", {indexPath}) << std::endl;
        }
    }

    int getNextId() {
        int maxId = 0;
        for (const auto& item : data) {
            if (item.contains("id") && item["id"].get<int>() > maxId) {
                maxId = item["id"].get<int>();
            }
        }
        return maxId + 1;
    }

public:
    ContestStore(const std::string& dir) : dataDir(dir) {
        loadIndex();
    }

    // 创建比赛
    int create(const std::string& title, const std::string& startTime,
               const std::string& endTime, const std::vector<int>& problemIds) {
        int id = getNextId();

        json contest = {
            {"id", id},
            {"title", title},
            {"start_time", startTime},
            {"end_time", endTime},
            {"problem_ids", problemIds},
            {"created_at", getCurrentTime()}
        };

        data.push_back(contest);
        saveIndex();

        // 保存比赛详情到单独文件
        ensureDataDir();
        std::ofstream f(getContestPath(id));
        if (f.is_open()) {
            f << contest.dump(2);
        }

        return id;
    }

    // 删除比赛
    bool deleteContest(int id) {
        for (auto it = data.begin(); it != data.end(); ++it) {
            if (it->contains("id") && (*it)["id"].get<int>() == id) {
                data.erase(it);
                saveIndex();

                // 删除比赛文件
                std::string path = getContestPath(id);
                if (fs::exists(path)) {
                    fs::remove(path);
                }
                return true;
            }
        }
        return false;
    }

    // 查看比赛
    json view(int id) {
        for (const auto& item : data) {
            if (item.contains("id") && item["id"].get<int>() == id) {
                // 加载完整详情
                std::string path = getContestPath(id);
                if (fs::exists(path)) {
                    std::ifstream f(path);
                    if (f.is_open()) {
                        json fullContest;
                        f >> fullContest;
                        return fullContest;
                    }
                }
                return item;
            }
        }
        return nullptr;
    }

    // 获取比赛中的题目ID
    int getContestProblemId(int contestId, int problemIndex) {
        json contest = view(contestId);
        if (contest.is_null()) return -1;

        if (contest.contains("problem_ids")) {
            const auto& problemIds = contest["problem_ids"];
            if (problemIndex >= 1 && problemIndex <= (int)problemIds.size()) {
                return problemIds[problemIndex - 1].get<int>();
            }
        }
        return -1;
    }

    // 提交比赛题目
    bool submitProblem(int contestId, int problemIndex, const std::string& filePath,
                       const std::string& username = "") {
        int problemId = getContestProblemId(contestId, problemIndex);
        if (problemId < 0) return false;

        // 加载题目数据进行评判
        std::string problemPath = dataDir + "/problems/problem_" + std::to_string(problemId) + ".json";
        json problemData;
        if (fs::exists(problemPath)) {
            std::ifstream pf(problemPath);
            if (pf.is_open()) {
                try {
                    pf >> problemData;
                } catch (...) {
                    problemData = json();
                }
            }
        }

        // 评判
        std::string result = "pending";
        int score = 0;
        int timeUsed = 0;
        int memoryUsed = 0;
        json judgeDetail = nullptr;

        if (!problemData.is_null() && fs::exists(filePath)) {
            auto judgeResult = clijudge::judge::judgeSubmission(problemData, filePath);
            result = clijudge::judge::statusToAbbr(judgeResult.status);
            score = judgeResult.totalScore;
            timeUsed = judgeResult.totalTimeMs;
            memoryUsed = judgeResult.maxMemoryKB;
            try {
                judgeDetail = clijudge::judge::resultToJson(judgeResult);
            } catch (...) {
            }
        }

        // 保存提交记录
        json submission = {
            {"contest_id", contestId},
            {"problem_index", problemIndex},
            {"problem_id", problemId},
            {"file_path", filePath},
            {"submitted_at", getCurrentTime()},
            {"result", result},
            {"score", score},
            {"time_used", timeUsed},
            {"memory_used", memoryUsed},
            {"judge_times", 1},
            {"username", username.empty() ? "unknown" : username}
        };
        if (!judgeDetail.is_null()) submission["judge_detail"] = judgeDetail;

        ensureDataDir();
        std::string submissionPath = dataDir + "/contests/contest_" + std::to_string(contestId) +
                                     "_problem_" + std::to_string(problemIndex) + "_submissions.json";

        json submissions = json::array();
        if (fs::exists(submissionPath)) {
            std::ifstream f(submissionPath);
            if (f.is_open()) {
                try {
                    f >> submissions;
                } catch (...) {
                    submissions = json::array();
                }
            }
        }
        submissions.push_back(submission);

        if (!platform::writeFileAtomic(submissionPath, submissions.dump(2))) {
            return false;
        }

        return true;
    }

    // 查看比赛题目提交记录
    json viewProblemSubmissions(int contestId, int problemIndex) {
        std::string submissionPath = dataDir + "/contests/contest_" + std::to_string(contestId) +
                                     "_problem_" + std::to_string(problemIndex) + "_submissions.json";

        if (fs::exists(submissionPath)) {
            std::ifstream f(submissionPath);
            if (f.is_open()) {
                try {
                    json submissions;
                    f >> submissions;
                    return submissions;
                } catch (...) {
                    return json::array();
                }
            }
        }
        return json::array();
    }

    // 列出所有比赛
    json list() {
        return data;
    }

    // 导入 CDF 格式比赛
    int importCdf(const std::string& cdfPath, const std::string& dataDir) {
        auto cdf = clijudge::cdf::parseCdf(cdfPath);
        if (cdf.tasks.empty()) {
            std::cerr << clijudge::lang::tr("contest.cdf_no_problems", "No problems in CDF file.") << std::endl;
            return -1;
        }
        // CDF 可能携带 SPJ/交互器/grader 源码（评测期以完全信任执行），导入前提示
        for (const auto& task : cdf.tasks) {
            if (!task.specialJudge.empty() || !task.interactor.empty() || !task.grader.empty()
                || !task.graderFilesPath.empty()) {
                std::cerr << clijudge::lang::tr("contest.cdf_special_code_warning", "Warning: this CDF contains special judge/interactor/generator/grader code, which is executed in a trusted (reduced-isolation) mode during judging. Only import contests from sources you trust.") << std::endl;
                break;
            }
        }

        // 获取 CDF 数据目录（cdf 文件所在目录下的 data/ 子目录）
        fs::path cdfDir = fs::path(cdfPath).parent_path();
        fs::path cdfDataDir = cdfDir / "data";

        // 创建 ProblemStore 来导入题目
        clijudge::problem::ProblemStore problemStore(dataDir);
        std::vector<int> problemIds;

        for (const auto& task : cdf.tasks) {
            // 构建题目 JSON
            json problemJson;
            problemJson["problem"]["title"] = task.problemTitle;
            problemJson["problem"]["time_limit"] = task.testCases.empty() ? 1000 : task.testCases[0].timeLimit;
            problemJson["problem"]["memory_limit"] = task.testCases.empty() ? 256 : task.testCases[0].memoryLimit;
            // 往返保留: 导出侧写入的 compareModeCliJudge/spjCode/float* 字段优先,
            // 否则回退到 Lemon 标准字段的映射 (第三方 CDF)
            problemJson["problem"]["compare_mode"] = !task.compareModeCliJudge.empty()
                ? task.compareModeCliJudge
                : clijudge::cdf::comparisonModeToCliJudge(task.comparisonMode);
            problemJson["problem"]["problem_type"] = clijudge::cdf::taskTypeToCliJudge(task.taskType);
            problemJson["problem"]["is_public"] = true;
            problemJson["problem"]["is_hidden"] = false;
            problemJson["problem"]["spj_code"] = task.spjCode;
            problemJson["problem"]["special_judge_exe"] = task.specialJudge;
            problemJson["problem"]["allowed_languages"] = json::array();
            // Lemon 每个 testcase = 一个子任务, 多文件取 min (等价 all_or_nothing)
            problemJson["problem"]["subtask_mode"] = "all_or_nothing";
            problemJson["problem"]["answer_file_extension"] =
                task.answerFileExtension.empty() ? "out" : task.answerFileExtension;
            problemJson["problem"]["source_file_name"] = task.sourceFileName;
            problemJson["problem"]["description"] = "";
            problemJson["problem"]["input_desc"] = "";
            problemJson["problem"]["output_desc"] = "";
            problemJson["problem"]["hint"] = "";
            problemJson["problem"]["sample_input"] = "";
            problemJson["problem"]["sample_output"] = "";

            if (task.hasFloatTol) {
                // 往返保留导出侧的精确 abs/rel 容差
                problemJson["problem"]["float_abs_tolerance"] = task.floatAbsTol;
                problemJson["problem"]["float_rel_tolerance"] = task.floatRelTol;
            } else if (task.comparisonMode == 3) {
                // RealNumber: eps = 10^-realPrecision, abs 与 rel 同用
                double eps = std::pow(10.0, -(double)(task.realPrecision > 0 ? task.realPrecision : 6));
                problemJson["problem"]["float_abs_tolerance"] = eps;
                problemJson["problem"]["float_rel_tolerance"] = eps;
            }

            std::string pType = clijudge::cdf::taskTypeToCliJudge(task.taskType);
            json graderFiles = json::object();

            // 交互题: interactor 源码 → interactor_code, 可执行文件 → interactor_data;
            // Lemon 的 task.grader (编译时附着在选手侧) → grader_files
            if (pType == "interaction") {
                auto resolve = [&](const std::string& rel) -> std::string {
                    if (rel.empty()) return "";
                    fs::path p1 = cdfDataDir / rel;
                    if (fs::exists(p1)) return p1.string();
                    if (fs::exists(fs::path(rel))) return fs::absolute(rel).string();
                    return "";
                };
                std::string interPath = resolve(task.interactor);
                if (!interPath.empty()) {
                    std::string ext = fs::path(interPath).extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char ch) { return (char)std::tolower(ch); });
                    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c") {
                        problemJson["problem"]["interactor_code"] =
                            clijudge::cdf::readFileContent(interPath);
                    } else {
                        problemJson["problem"]["interactor_data"] = interPath;
                    }
                }
                problemJson["problem"]["interactor_name"] = task.interactorName;
                std::string graderPath = resolve(task.grader);
                if (!graderPath.empty()) {
                    graderFiles[fs::path(task.grader).filename().generic_string()] =
                        clijudge::cdf::readFileContent(graderPath);
                }
            }

            // 通信题: grader 文件 → grader_files {相对名: 内容}
            if (pType == "communication" || pType == "communication_exec") {
                for (size_t i = 0; i < task.graderFilesPath.size(); i++) {
                    std::string rel = task.graderFilesPath[i];
                    fs::path gp = cdfDataDir / rel;
                    if (!fs::exists(gp)) continue;
                    std::string name = i < task.graderFilesName.size() && !task.graderFilesName[i].empty()
                                           ? task.graderFilesName[i]
                                           : fs::path(rel).filename().string();
                    std::replace(name.begin(), name.end(), '\\', '/');
                    if (name.empty() || name[0] == '/' || name.find(':') != std::string::npos ||
                        name.find("..") != std::string::npos)
                        continue;
                    graderFiles[name] = clijudge::cdf::readFileContent(gp.string());
                }
            }
            if (!graderFiles.empty()) problemJson["problem"]["grader_files"] = graderFiles;

            // 构建测试用例: 一个 Lemon testcase (可能多文件) → 一个子任务,
            // 每个 (input, output) 对拆为一个 CliJudge 测试点, 共享 subtask_id
            json testCases = json::array();
            json depMap = json::object();
            int cliIdx = 0;
            for (size_t t = 0; t < task.testCases.size(); t++) {
                const auto& tc = task.testCases[t];
                int subtaskId = (int)t + 1;

                // 依赖标记 → problem.subtask_dependence {"<子任务号>": [依赖子任务号]}
                if (!tc.dependenceSubtask.empty()) {
                    json deps = json::array();
                    for (int d : tc.dependenceSubtask) deps.push_back(d);
                    depMap[std::to_string(subtaskId)] = deps;
                }

                size_t n = std::min(tc.inputFiles.size(), tc.outputFiles.size());
                if (n == 0) n = std::max(tc.inputFiles.size(), tc.outputFiles.size());
                if (n == 0) n = 1;
                for (size_t j = 0; j < n; j++) {
                    json tcJson;
                    tcJson["id"] = ++cliIdx;
                    tcJson["score"] = tc.fullScore;
                    // 逐测试点时限/内存 (与导出侧对称); <=0 → -1 表示回退题目默认
                    tcJson["time_limit"] = tc.timeLimit > 0 ? tc.timeLimit : -1;
                    tcJson["memory_limit"] = tc.memoryLimit > 0 ? tc.memoryLimit : -1;
                    tcJson["sort_order"] = cliIdx;
                    tcJson["input_file"] = "";
                    tcJson["output_file"] = "";

                    std::string inputData, outputData;
                    if (j < tc.inputFiles.size())
                        inputData = clijudge::cdf::readFileContent((cdfDataDir / tc.inputFiles[j]).string());
                    if (j < tc.outputFiles.size())
                        outputData = clijudge::cdf::readFileContent((cdfDataDir / tc.outputFiles[j]).string());
                    tcJson["input_data"] = inputData;
                    tcJson["output_data"] = outputData;
                    tcJson["subtask_id"] = subtaskId;

                    testCases.push_back(tcJson);
                }
            }
            problemJson["test_cases"] = testCases;
            problemJson["problem"]["subtask_dependence"] = depMap;

            // 导入题目
            int problemId = problemStore.importProblem(problemJson);
            if (problemId > 0) {
                problemIds.push_back(problemId);
                std::cout << clijudge::lang::trf("contest.imported_problem",
                                                 "  Imported problem: {0} (ID: {1})",
                                                 {task.problemTitle, std::to_string(problemId)}) << std::endl;
            } else {
                std::cerr << clijudge::lang::trf("contest.import_problem_failed", "  Failed to import problem: {0}",
                                                 {task.problemTitle}) << std::endl;
            }
        }

        // 创建比赛
        if (problemIds.empty()) {
            std::cerr << clijudge::lang::tr("contest.no_problems_imported", "No problems imported successfully.") << std::endl;
            return -1;
        }

        int contestId = create(cdf.contestTitle, "", "", problemIds);
        std::cout << clijudge::lang::trf("contest.imported", "Contest imported: {0} (ID: {1})",
                                         {cdf.contestTitle, std::to_string(contestId)}) << std::endl;
        return contestId;
    }

    // 导出为 CDF 格式; 测试数据/交互器/grader 写入 <cdfPath 同级>/data/
    json exportCdf(int contestId, const std::string& cdfPath) {
        json contest = view(contestId);
        if (contest.is_null()) return nullptr;

        clijudge::problem::ProblemStore problemStore(dataDir);

        fs::path outDir = fs::path(cdfPath).parent_path();
        if (outDir.empty()) outDir = ".";
        fs::path dataPath = outDir / "data";
        std::error_code dirEc;
        fs::create_directories(dataPath, dirEc);
        if (dirEc) {
            std::cerr << clijudge::lang::trf("contest.data_dir_create_failed", "Failed to create data directory: {0} ({1})",
                                             {dataPath.string(), dirEc.message()}) << std::endl;
            return nullptr;
        }

        auto writeFile = [](const fs::path& path, const std::string& content) -> bool {
            std::error_code cec;
            fs::create_directories(path.parent_path(), cec);
            std::ofstream f(path, std::ios::binary);
            if (!f.is_open()) return false;
            f.write(content.data(), (std::streamsize)content.size());
            return f.good();
        };

        json cdf;
        cdf["version"] = "1.0";
        cdf["contestTitle"] = contest.value("title", "");

        json tasks = json::array();
        if (contest.contains("problem_ids")) {
            for (const auto& pid : contest["problem_ids"]) {
                int problemId = pid.get<int>();
                json problem = problemStore.view(problemId);
                if (problem.is_null()) continue;

                const auto& p = problem["problem"];
                const auto& testCases = problem.value("test_cases", json::array());

                std::string pType = p.value("problem_type", "traditional");
                std::string srcName = p.value("source_file_name", "");
                if (srcName.empty()) srcName = p.value("title", "solution");

                // 数据目录名: 去掉路径分隔符, 避免写出 data 目录外
                std::string dirName = srcName;
                for (auto& ch : dirName) {
                    if (ch == '/' || ch == '\\' || ch == ':' || ch == '<' || ch == '>' || ch == '|' ||
                        ch == '*' || ch == '?' || ch == '"')
                        ch = '_';
                }
                if (dirName.empty() || dirName == "." || dirName == "..") dirName = "task";

                json task;
                task["problemTitle"] = p.value("title", "");
                task["sourceFileName"] = srcName;
                task["inputFileName"] = srcName + ".in";
                task["outputFileName"] = srcName + ".out";
                task["standardInputCheck"] = true;
                task["standardOutputCheck"] = true;
                task["taskType"] = clijudge::cdf::cliJudgeToTaskType(pType);
                task["subFolderCheck"] = false;
                std::string cjMode = p.value("compare_mode", "text_strict");
                task["comparisonMode"] = clijudge::cdf::cliJudgeToComparisonMode(cjMode);
                // CliJudge 扩展字段: 往返保留精确 compare_mode / spj_code / 容差
                task["compareModeCliJudge"] = cjMode;
                task["spjCode"] = p.value("spj_code", "");
                task["diffArguments"] = "--ignore-space-change --text --brief";
                double ftol = p.value("float_abs_tolerance", 0.0);
                double rtol = p.value("float_rel_tolerance", 0.0);
                task["floatAbsTol"] = ftol;
                task["floatRelTol"] = rtol;
                int realPrecision = 3;
                double ptol = ftol > 0.0 ? ftol : rtol;  // abs 缺失时按 rel 推导 (Lemon 单值精度)
                if (ptol > 0.0) {
                    int e = (int)std::lround(-std::log10(ptol));
                    if (e >= 0 && e <= 15) realPrecision = e;
                }
                task["realPrecision"] = realPrecision;
                task["specialJudge"] = p.value("special_judge_exe", "");
                task["answerFileExtension"] = p.value("answer_file_extension", "out");
                task["compilerConfiguration"] = json::object();

                // 交互题: interactor / grader (Lemon 单文件字段)
                if (pType == "interaction") {
                    std::string iCode = p.value("interactor_code", "");
                    std::string iData = p.value("interactor_data", "");
                    if (!iCode.empty()) {
                        fs::path ip = dataPath / dirName / "interactor.cpp";
                        if (writeFile(ip, iCode)) {
                            task["interactor"] = dirName + "/interactor.cpp";
                            task["interactorName"] = "interactor.cpp";
                        }
                    } else if (!iData.empty() && fs::exists(fs::path(iData))) {
                        fs::path dst = dataPath / dirName / fs::path(iData).filename();
                        std::error_code cec;
                        fs::create_directories(dst.parent_path(), cec);
                        fs::copy_file(iData, dst, fs::copy_options::overwrite_existing, cec);
                        if (!cec) {
                            task["interactor"] = dirName + "/" + dst.filename().generic_string();
                            task["interactorName"] = dst.filename().string();
                        }
                    }
                    json gf = p.value("grader_files", json::object());
                    if (gf.is_object() && !gf.empty()) {
                        auto it = gf.begin();
                        std::string gkey = it.key();
                        std::replace(gkey.begin(), gkey.end(), '\\', '/');
                        if (it->is_string() && !gkey.empty() && gkey[0] != '/' &&
                            gkey.find("..") == std::string::npos && gkey.find(':') == std::string::npos) {
                            if (writeFile(dataPath / dirName / gkey, it.value().get<std::string>()))
                                task["grader"] = dirName + "/" + gkey;
                        }
                    }
                }

                // 通信题: grader 文件数组 + 选手源文件名 (Lemon 过滤器)
                if (pType == "communication" || pType == "communication_exec") {
                    json gf = p.value("grader_files", json::object());
                    json gPaths = json::array(), gNames = json::array();
                    if (gf.is_object()) {
                        for (auto it = gf.begin(); it != gf.end(); ++it) {
                            std::string gkey = it.key();
                            std::replace(gkey.begin(), gkey.end(), '\\', '/');
                            if (!it->is_string() || gkey.empty() || gkey[0] == '/' ||
                                gkey.find("..") != std::string::npos || gkey.find(':') != std::string::npos)
                                continue;
                            if (!writeFile(dataPath / dirName / gkey, it.value().get<std::string>()))
                                continue;
                            gPaths.push_back(dirName + "/" + gkey);
                            gNames.push_back(gkey);
                        }
                    }
                    if (!gPaths.empty()) {
                        task["graderFilesPath"] = gPaths;
                        task["graderFilesName"] = gNames;
                    }
                    // 选手提交文件名 (Lemon 用作过滤器, 无法预知提交扩展名 → 常见 .cpp)
                    std::string sName = srcName;
                    if (fs::path(sName).extension().empty()) sName += ".cpp";
                    task["sourceFilesPath"] = json::array({sName});
                    task["sourceFilesName"] = json::array({sName});
                }

                // 测试点: 每个 CliJudge 测试点 → 一个 Lemon 测试点 (保留每点分数/部分分)
                std::vector<const json*> sorted;
                sorted.reserve(testCases.size());
                for (const auto& tc : testCases) sorted.push_back(&tc);
                std::stable_sort(sorted.begin(), sorted.end(),
                                 [](const json* a, const json* b) {
                                     return a->value("sort_order", 0) < b->value("sort_order", 0);
                                 });

                // subtask_id → 该子任务首个测试点的 Lemon 序号 (依赖标记用)
                std::map<int, int> sidFirstCase;
                int caseNo = 0;
                for (const json* tc : sorted) {
                    caseNo++;
                    int sid = tc->value("subtask_id", 1);
                    if (sidFirstCase.find(sid) == sidFirstCase.end()) sidFirstCase[sid] = caseNo;
                }

                json cdfTestCases = json::array();
                int n = 0;
                for (const json* tc : sorted) {
                    n++;
                    std::string inRel = dirName + "/" + std::to_string(n) + ".in";
                    std::string outRel = dirName + "/" + std::to_string(n) + ".ans";
                    writeFile(dataPath / inRel, tc->value("input_data", ""));
                    writeFile(dataPath / outRel, tc->value("output_data", ""));

                    int tl = tc->value("time_limit", -1);
                    if (tl <= 0) tl = p.value("time_limit", 1000);
                    int ml = tc->value("memory_limit", -1);
                    if (ml <= 0) ml = p.value("memory_limit", 256);

                    json cdfTc;
                    cdfTc["fullScore"] = tc->value("score", 0);
                    cdfTc["timeLimit"] = tl;
                    cdfTc["memoryLimit"] = ml;
                    json inputs = json::array({inRel});

                    int sid = tc->value("subtask_id", 1);
                    auto dmIt = p.find("subtask_dependence");
                    if (dmIt != p.end() && dmIt->is_object()) {
                        auto dIt = dmIt->find(std::to_string(sid));
                        if (dIt != dmIt->end() && dIt->is_array()) {
                            for (const auto& depIdJson : *dIt) {
                                if (!depIdJson.is_number_integer()) continue;
                                auto fIt = sidFirstCase.find(depIdJson.get<int>());
                                if (fIt == sidFirstCase.end()) continue;
                                inputs.push_back(std::to_string(fIt->second) +
                                                 "_lemon_SUbtaskDEPENDENCE_fLAg");
                            }
                        }
                    }

                    cdfTc["inputFiles"] = inputs;
                    cdfTc["outputFiles"] = json::array({outRel});
                    cdfTestCases.push_back(cdfTc);
                }
                task["testCases"] = cdfTestCases;

                tasks.push_back(task);
            }
        }
        cdf["tasks"] = tasks;
        cdf["contestants"] = json::array();

        return cdf;
    }

    // 获取比赛排行榜
    json leaderboard(int contestId) {
        json contest = view(contestId);
        if (contest.is_null()) return nullptr;

        json rankings = json::array();

        if (contest.contains("problem_ids")) {
            const auto& problemIds = contest["problem_ids"];
            int numProblems = (int)problemIds.size();

            // 收集所有提交; 总分按 (选手, 题目) 取最优一次计入 (与 reportHtml 一致),
            // 同分时优先取 AC 条目; total 为提交次数
            std::map<std::string, json> userStats;
            std::map<std::string, std::map<int, std::pair<int, bool>>> best; // user -> pIdx -> {bestScore, isAC}
            for (int i = 1; i <= numProblems; i++) {
                json submissions = viewProblemSubmissions(contestId, i);
                if (!submissions.is_array()) continue;
                for (const auto& sub : submissions) {
                    std::string user = sub.value("username", "unknown");
                    if (userStats.find(user) == userStats.end()) {
                        userStats[user] = {
                            {"username", user},
                            {"score", 0},
                            {"accepted", 0},
                            {"total", 0}
                        };
                    }
                    userStats[user]["total"] = userStats[user]["total"].get<int>() + 1;
                    std::string result = sub.value("result", "");
                    int score = sub.value("score", 0);
                    bool isAC = (result == "AC" || result == "accepted");
                    auto& slot = best[user][i];
                    if (score > slot.first || (score == slot.first && isAC && !slot.second)) {
                        slot.first = score;
                        slot.second = isAC;
                    }
                }
            }

            // 按最优条目汇总
            for (auto& [user, st] : userStats) {
                int totalScore = 0, accepted = 0;
                for (auto& [pIdx, slot] : best[user]) {
                    totalScore += slot.first;
                    if (slot.second) accepted++;
                }
                st["score"] = totalScore;
                st["accepted"] = accepted;
            }

            // 转为数组并排序
            for (auto& [key, val] : userStats) {
                rankings.push_back(val);
            }
            std::sort(rankings.begin(), rankings.end(),
                [](const json& a, const json& b) {
                    return a["score"].get<int>() > b["score"].get<int>();
                });
        }

        return rankings;
    }

    // 生成比赛报告 HTML (排名 + 题目 + 提交明细含 judge_detail)
    std::string reportHtml(int contestId) {
        json contest = view(contestId);
        if (contest.is_null()) return "";
        clijudge::problem::ProblemStore problemStore(dataDir);
        int nP = contest.contains("problem_ids") ? (int)contest["problem_ids"].size() : 0;

        std::vector<std::string> titles((size_t)std::max(nP, 0), "");
        std::vector<int> maxScores((size_t)std::max(nP, 0), 0);
        for (int i = 0; i < nP; i++) {
            json p = problemStore.view(contest["problem_ids"][i].get<int>());
            if (p.is_null()) continue;
            titles[i] = p["problem"].value("title", "");
            for (const auto& tc : p.value("test_cases", json::array()))
                maxScores[i] += tc.value("score", 0);
        }

        auto esc = [](const std::string& s) {
            std::string o;
            o.reserve(s.size());
            for (char c : s) {
                switch (c) {
                    case '&': o += "&amp;"; break;
                    case '<': o += "&lt;"; break;
                    case '>': o += "&gt;"; break;
                    case '"': o += "&quot;"; break;
                    default: o += c;
                }
            }
            return o;
        };
        auto detailText = [](const json& d) -> std::string {
            if (!d.is_object()) return "";
            std::string o;
            std::string ce = d.value("compile_error", "");
            if (!ce.empty()) o += "Compile Error:\n" + ce + "\n\n";
            for (const auto& st : d.value("subtasks", json::array())) {
                o += "Subtask " + std::to_string(st.value("id", 0)) + ": " +
                     std::to_string(st.value("score", 0)) + "/" + std::to_string(st.value("max_score", 0)) +
                     " " + st.value("status_abbr", "") + "\n";
                for (const auto& tc : st.value("test_cases", json::array())) {
                    o += "  #" + std::to_string(tc.value("id", 0)) + " " + tc.value("status_abbr", "") + " " +
                         std::to_string(tc.value("score", 0)) + "/" + std::to_string(tc.value("max_score", 0)) + " " +
                         std::to_string(tc.value("time_ms", 0)) + "ms " +
                         std::to_string(tc.value("memory_kb", 0)) + "KB";
                    std::string msg = tc.value("message", "");
                    if (!msg.empty()) o += " - " + msg;
                    o += "\n";
                }
            }
            return o;
        };

        struct Row {
            int pIdx;
            std::string at, user, result;
            int score, timeUsed, mem, jtimes;
            json detail;
        };
        std::vector<Row> rows;
        std::vector<std::string> users;
        std::map<std::string, std::map<int, size_t>> best;

        for (int i = 0; i < nP; i++) {
            json subs = viewProblemSubmissions(contestId, i + 1);
            if (!subs.is_array()) continue;
            for (const auto& sub : subs) {
                Row r;
                r.pIdx = i;
                r.at = sub.value("submitted_at", "");
                r.user = sub.value("username", "unknown");
                r.result = sub.value("result", "pending");
                r.score = sub.value("score", 0);
                r.timeUsed = sub.value("time_used", 0);
                r.mem = sub.value("memory_used", 0);
                r.jtimes = sub.value("judge_times", 1);
                r.detail = sub.value("judge_detail", json());
                rows.push_back(r);
                size_t idx = rows.size() - 1;
                if (std::find(users.begin(), users.end(), r.user) == users.end()) users.push_back(r.user);
                auto slotIt = best[r.user].find(r.pIdx);
                if (slotIt == best[r.user].end()) {
                    best[r.user][r.pIdx] = idx;
                } else {
                    const Row& cur = rows[slotIt->second];
                    if (r.score > cur.score ||
                        (r.score == cur.score && r.result == "AC" && cur.result != "AC"))
                        slotIt->second = idx;
                }
            }
        }

        std::vector<std::pair<std::string, int>> totals;
        for (const auto& u : users) {
            int t = 0;
            for (const auto& [pi, idx] : best[u]) t += rows[idx].score;
            totals.push_back({u, t});
        }
        std::stable_sort(totals.begin(), totals.end(),
                         [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
                             return a.second != b.second ? a.second > b.second : a.first < b.first;
                         });

        std::string H;
        H += "<!DOCTYPE html>\n<html lang=\"zh\">\n<head>\n<meta charset=\"utf-8\">\n";
        H += "<title>比赛报告 - " + esc(contest.value("title", "")) + "</title>\n<style>\n";
        H += "body{font-family:'Segoe UI',Arial,sans-serif;margin:24px;color:#222;}\n";
        H += "h1{border-bottom:2px solid #4a90d9;padding-bottom:8px;}\n";
        H += "table{border-collapse:collapse;margin:12px 0;}\n";
        H += "th,td{border:1px solid #bbb;padding:5px 10px;text-align:center;font-size:14px;}\n";
        H += "th{background:#eef3fa;}\n";
        H += ".st-AC{background:#c8e6c9;} .st-WA{background:#ffcdd2;} .st-PE{background:#ffe0b2;}\n";
        H += ".st-TLE,.st-MLE,.st-RE{background:#ffcc80;}\n";
        H += ".st-CE{background:#cfd8dc;} .st-SE,.st-IE,.st-SK{background:#e1bee7;}\n";
        H += ".st-PC{background:#fff9c4;} .st-OLE,.st-ST,.st-SR{background:#b3e5fc;}\n";
        H += ".muted{color:#777;font-size:13px;}\n";
        H += "details{text-align:left;}\npre{background:#f6f8fa;padding:8px;border:1px solid #ddd;white-space:pre-wrap;}\n";
        H += "</style>\n</head>\n<body>\n";

        H += "<h1>比赛报告: " + esc(contest.value("title", "")) + "</h1>\n";
        H += "<p class=\"muted\">时间: " + esc(contest.value("start_time", "")) + " ~ " +
             esc(contest.value("end_time", "")) + " · 生成于 " + esc(getCurrentTime()) + "</p>\n";

        H += "<h2>题目</h2>\n<table>\n<tr><th>#</th><th>标题</th><th>满分</th></tr>\n";
        for (int i = 0; i < nP; i++) {
            H += "<tr><td>" + std::to_string(i + 1) + "</td><td>" + esc(titles[(size_t)i]) + "</td><td>" +
                 std::to_string(maxScores[(size_t)i]) + "</td></tr>\n";
        }
        H += "</table>\n";

        H += "<h2>排名</h2>\n";
        if (totals.empty()) {
            H += "<p class=\"muted\">暂无提交</p>\n";
        } else {
            H += "<table>\n<tr><th>名次</th><th>选手</th>";
            for (int i = 0; i < nP; i++) H += "<th>P" + std::to_string(i + 1) + "</th>";
            H += "<th>总分</th></tr>\n";
            for (size_t ti = 0; ti < totals.size(); ti++) {
                const auto& user = totals[ti].first;
                H += "<tr><td>" + std::to_string(ti + 1) + "</td><td>" + esc(user) + "</td>";
                for (int i = 0; i < nP; i++) {
                    auto slotIt = best[user].find(i);
                    if (slotIt == best[user].end()) {
                        H += "<td>-</td>";
                    } else {
                        const Row& r = rows[slotIt->second];
                        H += "<td class=\"st-" + esc(r.result) + "\">" + std::to_string(r.score) + " " +
                             esc(r.result) + "</td>";
                    }
                }
                H += "<td><b>" + std::to_string(totals[ti].second) + "</b></td></tr>\n";
            }
            H += "</table>\n";
        }

        H += "<h2>提交记录</h2>\n";
        if (rows.empty()) {
            H += "<p class=\"muted\">暂无提交</p>\n";
        } else {
            H += "<table>\n<tr><th>时间</th><th>选手</th><th>题目</th><th>状态</th><th>得分</th>";
            H += "<th>时间(ms)</th><th>内存(KB)</th><th>评测次数</th><th>详情</th></tr>\n";
            for (const auto& r : rows) {
                std::string dt = detailText(r.detail);
                H += "<tr><td>" + esc(r.at) + "</td><td>" + esc(r.user) + "</td><td>P" +
                     std::to_string(r.pIdx + 1) + " " + esc(titles[(size_t)r.pIdx]) + "</td>" +
                     "<td class=\"st-" + esc(r.result) + "\">" + esc(r.result) + "</td><td>" +
                     std::to_string(r.score) + "</td><td>" + std::to_string(r.timeUsed) + "</td><td>" +
                     std::to_string(r.mem) + "</td><td>" + std::to_string(r.jtimes) + "</td><td>";
                if (dt.empty()) {
                    H += "-";
                } else {
                    H += "<details><summary>查看</summary><pre>" + esc(dt) + "</pre></details>";
                }
                H += "</td></tr>\n";
            }
            H += "</table>\n";
        }

        H += "</body>\n</html>\n";
        return H;
    }

private:
    std::string getCurrentTime() {
        time_t now = time(nullptr);
        char buf[64];
        struct tm timeinfo;
        clijudge::platform::localTime(&now, &timeinfo);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        return std::string(buf);
    }
};

// 子命令实现
inline int cmdCreate(const std::string& dataDir, const std::string& title,
                     const std::string& startTime, const std::string& endTime,
                     const std::vector<int>& problemIds) {
    ContestStore store(dataDir);
    int id = store.create(title, startTime, endTime, problemIds);
    std::cout << clijudge::lang::trf("contest.created", "Contest created with ID: {0}", {std::to_string(id)}) << std::endl;
    return 0;
}

inline int cmdDelete(const std::string& dataDir, int id) {
    ContestStore store(dataDir);
    if (store.deleteContest(id)) {
        std::cout << clijudge::lang::trf("contest.deleted", "Contest {0} deleted.", {std::to_string(id)}) << std::endl;
        return 0;
    } else {
        std::cerr << clijudge::lang::trf("contest.not_found", "Contest {0} not found.", {std::to_string(id)}) << std::endl;
        return 1;
    }
}

inline int cmdView(const std::string& dataDir, int id) {
    ContestStore store(dataDir);
    json contest = store.view(id);
    if (contest.is_null()) {
        std::cerr << clijudge::lang::trf("contest.not_found", "Contest {0} not found.", {std::to_string(id)}) << std::endl;
        return 1;
    }
    std::cout << contest.dump(2) << std::endl;
    return 0;
}

inline int cmdProblemSubmit(const std::string& dataDir, int contestId,
                            int problemIndex, const std::string& filePath,
                            const std::string& username = "") {
    ContestStore store(dataDir);
    if (store.submitProblem(contestId, problemIndex, filePath, username)) {
        std::cout << clijudge::lang::trf("contest.submission_accepted",
                                         "Submission accepted for contest {0} problem {1}",
                                         {std::to_string(contestId), std::to_string(problemIndex)}) << std::endl;
        std::cout << clijudge::lang::trf("contest.submission_user", "  User: {0}",
                                         {username.empty() ? "unknown" : username}) << std::endl;
        return 0;
    } else {
        std::cerr << clijudge::lang::tr("contest.submit_failed", "Failed to submit: invalid contest or problem index.") << std::endl;
        return 1;
    }
}

inline int cmdProblemView(const std::string& dataDir, int contestId, int problemIndex) {
    ContestStore store(dataDir);
    json submissions = store.viewProblemSubmissions(contestId, problemIndex);
    std::cout << submissions.dump(2) << std::endl;
    return 0;
}

inline int cmdList(const std::string& dataDir) {
    ContestStore store(dataDir);
    json contests = store.list();
    std::cout << contests.dump(2) << std::endl;
    return 0;
}

inline int cmdLeaderboard(const std::string& dataDir, int contestId) {
    ContestStore store(dataDir);
    json board = store.leaderboard(contestId);
    if (board.is_null()) {
        std::cerr << clijudge::lang::trf("contest.not_found", "Contest {0} not found.", {std::to_string(contestId)}) << std::endl;
        return 1;
    }
    std::cout << board.dump(2) << std::endl;
    return 0;
}

inline int cmdReport(const std::string& dataDir, int contestId, const std::string& outPath = "") {
    ContestStore store(dataDir);
    std::string html = store.reportHtml(contestId);
    if (html.empty()) {
        std::cerr << clijudge::lang::trf("contest.not_found", "Contest {0} not found.", {std::to_string(contestId)}) << std::endl;
        return 1;
    }
    std::string path = outPath.empty() ? ("contest_" + std::to_string(contestId) + "_report.html") : outPath;
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << clijudge::lang::trf("contest.file_create_failed", "Failed to create file: {0}", {path}) << std::endl;
        return 1;
    }
    f << html;
    f.close();
    std::cout << clijudge::lang::trf("contest.report_generated", "Contest report generated: {0}", {path}) << std::endl;
    return 0;
}

inline int cmdImportCdf(const std::string& dataDir, const std::string& cdfPath) {
    if (!fs::exists(cdfPath)) {
        std::cerr << clijudge::lang::trf("contest.cdf_not_found", "CDF file not found: {0}", {cdfPath}) << std::endl;
        return 1;
    }
    ContestStore store(dataDir);
    int contestId = store.importCdf(cdfPath, dataDir);
    if (contestId > 0) {
        return 0;
    }
    return 1;
}

inline int cmdExportCdf(const std::string& dataDir, int contestId, const std::string& cdfPath) {
    ContestStore store(dataDir);
    json cdf = store.exportCdf(contestId, cdfPath);
    if (cdf.is_null()) {
        std::cerr << clijudge::lang::trf("contest.not_found", "Contest {0} not found.", {std::to_string(contestId)}) << std::endl;
        return 1;
    }

    std::ofstream f(cdfPath);
    if (!f.is_open()) {
        std::cerr << clijudge::lang::trf("contest.file_create_failed", "Failed to create file: {0}", {cdfPath}) << std::endl;
        return 1;
    }
    f << cdf.dump(-1);  // 紧凑格式
    f.close();
    std::cout << clijudge::lang::trf("contest.exported_to", "Contest exported to: {0}", {cdfPath}) << std::endl;
    return 0;
}

} // namespace contest
} // namespace clijudge

#endif // CLIJUDGE_CONTEST_H