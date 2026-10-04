// main.cpp
// CliJudge 轻量级命令行在线评测系统主入口
//
// 用法: clijudge.exe <子命令> [参数...]
//
// 子命令列表:
//   article
//     count                          统计文章数量
//     create [标题] [md文件路径]     创建文章
//     delete [编号]                  删除文章
//     list [L编号=1] [R编号=50]      列出文章
//     view [编号]                    查看文章
//
//   contest
//     create [标题] [开始时间] [结束时间] [题目1] [题目2]  创建比赛
//     delete [编号]                  删除比赛
//     problem [编号] [题目在比赛中的编号]
//       submit [文件地址]            提交题目
//       view                         查看题目
//     view [编号]                    查看比赛
//     list                           列出比赛
//     leaderboard [编号]             比赛排名
//     report [编号] [out.html]       导出比赛报告 (HTML)
//     import [cdf路径]               导入 CDF 比赛
//     export [编号] [cdf路径]        导出 CDF 比赛
//
//   ide
//     run [代码路径] [in文件路径]    运行代码
//
//   problem
//     count                          统计题目数量
//     create [标题]                  创建题目
//     delete [编号]                  删除题目
//     edit [编号]                    编辑题目
//     export [zip路径]               导出题目
//     import [zip路径]               导入题目
//     list [L=1] [R=50]              列出题目
//     submit [编号] [程序文件路径]   提交题目
//     testdata [题目编号]
//       -set-all                     设置所有测试数据
//       -zip [zip路径]               从zip导入测试数据
//       create [in] [out] [time] [mem] [pts]  创建测试数据
//       delete [编号]                删除测试数据
//       list                         列出测试数据
//     view [编号]                    查看题目
//
//   submit
//     count                          统计提交数量
//     list [L=1] [R=50]              列出提交记录
//     rejudge [提交编号]             重新评判提交

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <filesystem>
#include "json.hpp"
#include "platform.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "miniz_impl.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "article.h"
#include "contest.h"
#include "ide.h"
#include "problem.h"
#include "submit.h"
#include "lang.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// 默认数据目录（与 lang::dataDir 同源：环境变量优先，其次 exe 目录/data）
std::string getDataDir() {
    return clijudge::lang::dataDir();
}

// 显示帮助信息
void showHelp() {
    std::cout << clijudge::lang::tr("help.title", "CliJudge - Lightweight Command-Line Judge System") << std::endl;
    std::cout << std::endl;
    std::cout << clijudge::lang::tr("help.usage", "Usage: clijudge.exe <command> [arguments...]") << std::endl;
    std::cout << std::endl;
    std::cout << clijudge::lang::tr("help.commands", "Commands:") << std::endl;
    std::cout << "  article   - " << clijudge::lang::tr("help.article", "Article management") << std::endl;
    std::cout << "  contest   - " << clijudge::lang::tr("help.contest", "Contest management") << std::endl;
    std::cout << "  ide       - " << clijudge::lang::tr("help.ide", "IDE functions") << std::endl;
    std::cout << "  problem   - " << clijudge::lang::tr("help.problem", "Problem management") << std::endl;
    std::cout << "  submit    - " << clijudge::lang::tr("help.submit", "Submission management") << std::endl;
    std::cout << "  displaylang - " << clijudge::lang::tr("help.displaylang", "Display language settings") << std::endl;
    std::cout << std::endl;
    std::cout << clijudge::lang::tr("help.more_info", "Use 'clijudge.exe <command> help' for more information about a command.") << std::endl;
}

// 显示子命令帮助
void showCommandHelp(const std::string& command) {
    if (command == "article") {
        std::cout << clijudge::lang::tr("article.commands", "Article Commands:") << std::endl;
        std::cout << "  count                          " << clijudge::lang::tr("article.count", "Count articles") << std::endl;
        std::cout << "  create [title] [md_file]       " << clijudge::lang::tr("article.create", "Create article") << std::endl;
        std::cout << "  delete [id]                    " << clijudge::lang::tr("article.delete", "Delete article") << std::endl;
        std::cout << "  list [L=1] [R=50]              " << clijudge::lang::tr("article.list", "List articles") << std::endl;
        std::cout << "  view [id]                      " << clijudge::lang::tr("article.view", "View article") << std::endl;
    } else if (command == "contest") {
        std::cout << clijudge::lang::tr("contest.commands", "Contest Commands:") << std::endl;
        std::cout << "  create [title] [start] [end] [prob1] [prob2]  " << clijudge::lang::tr("contest.create", "Create contest") << std::endl;
        std::cout << "  delete [id]                    " << clijudge::lang::tr("contest.delete", "Delete contest") << std::endl;
        std::cout << "  export [id] [cdf_path]         " << clijudge::lang::tr("contest.export", "Export contest to CDF") << std::endl;
        std::cout << "  import [cdf_path]              " << clijudge::lang::tr("contest.import", "Import contest from CDF") << std::endl;
        std::cout << "  leaderboard [id]               " << clijudge::lang::tr("contest.leaderboard", "View contest leaderboard") << std::endl;
        std::cout << "  list                           " << clijudge::lang::tr("contest.list", "List contests") << std::endl;
        std::cout << "  report [id] [out_html]         " << clijudge::lang::tr("contest.report", "Export contest report (HTML)") << std::endl;
        std::cout << "  problem [id] [prob_index]      " << clijudge::lang::tr("contest.problem", "Contest problem") << std::endl;
        std::cout << "    submit [file] [--as user]    " << clijudge::lang::tr("contest.submit", "Submit solution") << std::endl;
        std::cout << "    view                         " << clijudge::lang::tr("contest.view_submissions", "View submissions") << std::endl;
        std::cout << "  view [id]                      " << clijudge::lang::tr("contest.view", "View contest") << std::endl;
    } else if (command == "ide") {
        std::cout << clijudge::lang::tr("ide.commands", "IDE Commands:") << std::endl;
        std::cout << "  run [code] [input]             " << clijudge::lang::tr("ide.run", "Run code") << std::endl;
    } else if (command == "problem") {
        std::cout << clijudge::lang::tr("problem.commands", "Problem Commands:") << std::endl;
        std::cout << "  count                          " << clijudge::lang::tr("problem.count", "Count problems") << std::endl;
        std::cout << "  create [title]                 " << clijudge::lang::tr("problem.create", "Create problem") << std::endl;
        std::cout << clijudge::lang::tr("problem.create_options_1", "    options: -type -compare -spj-code -spj-exe -float-abs -float-rel") << std::endl;
        std::cout << clijudge::lang::tr("problem.create_options_2", "             -subtask-mode -answer-ext -source-name -dependence") << std::endl;
        std::cout << clijudge::lang::tr("problem.create_options_3", "             -interactor -grader -background -describe -exampleio") << std::endl;
        std::cout << clijudge::lang::tr("problem.create_options_4", "             -generator -generator-exe -instyle -outstyle") << std::endl;
        std::cout << "  delete [id]                    " << clijudge::lang::tr("problem.delete", "Delete problem") << std::endl;
        std::cout << "  edit [id]                      " << clijudge::lang::tr("problem.edit", "Edit problem (same options as create)") << std::endl;
        std::cout << "  export [zip_path]              " << clijudge::lang::tr("problem.export", "Export problem") << std::endl;
        std::cout << "  import [zip_path]              " << clijudge::lang::tr("problem.import", "Import problem") << std::endl;
        std::cout << "  list [L=1] [R=50]              " << clijudge::lang::tr("problem.list", "List problems") << std::endl;
        std::cout << "  submit [id] [file] [--as user] " << clijudge::lang::tr("problem.submit", "Submit solution") << std::endl;
        std::cout << "  testdata [id]                  " << clijudge::lang::tr("problem.testdata", "Test data management") << std::endl;
        std::cout << "    -set-all                     " << clijudge::lang::tr("problem.set_all", "Set all test data defaults") << std::endl;
        std::cout << "    -zip [zip_path]              " << clijudge::lang::tr("problem.import_zip", "Import test data from zip") << std::endl;
        std::cout << "    create [in] [out] [time] [mem] [pts]  " << clijudge::lang::tr("problem.create_test", "Create test case") << std::endl;
        std::cout << "    delete [id]                  " << clijudge::lang::tr("problem.delete_test", "Delete test case") << std::endl;
        std::cout << "    list                         " << clijudge::lang::tr("problem.list_test", "List test cases") << std::endl;
        std::cout << "  view [id]                      " << clijudge::lang::tr("problem.view", "View problem") << std::endl;
    } else if (command == "submit") {
        std::cout << clijudge::lang::tr("submit.commands", "Submit Commands:") << std::endl;
        std::cout << "  count                          " << clijudge::lang::tr("submit.count", "Count submissions") << std::endl;
        std::cout << "  list [L=1] [R=50]              " << clijudge::lang::tr("submit.list", "List submissions") << std::endl;
        std::cout << "  rejudge [id]                   " << clijudge::lang::tr("submit.rejudge", "Rejudge submission") << std::endl;
    } else if (command == "displaylang") {
        std::cout << clijudge::lang::tr("displaylang.commands", "Display Language Commands:") << std::endl;
        std::cout << "  list                           " << clijudge::lang::tr("displaylang.list", "List local languages") << std::endl;
        std::cout << "  list --online                  " << clijudge::lang::tr("displaylang.list_online", "List online languages") << std::endl;
        std::cout << "  switch [langname]              " << clijudge::lang::tr("displaylang.switch", "Switch display language") << std::endl;
        std::cout << "  delete [langname]              " << clijudge::lang::tr("displaylang.delete", "Delete local language") << std::endl;
        std::cout << "  pull [langname]                " << clijudge::lang::tr("displaylang.pull", "Pull language (no switch)") << std::endl;
std::cout << "  source [github|gitee|custom]  " << clijudge::lang::tr("displaylang.source", "Language pack source") << std::endl;
    } else {
        showHelp();
    }
}

// 解析整数参数
int parseInt(const char* str, int defaultValue = 0) {
    try {
        return std::stoi(str);
    } catch (...) {
        return defaultValue;
    }
}

// 判断是否为修改类命令（需要持有跨进程数据锁）。
// 读命令不加锁：索引/配置均为原子替换写入，读到的总是完整快照。
bool isMutationCommand(const std::string& command, int argc, char* argv[]) {
    if (argc < 3) return false;
    std::string sub = argv[2];
    if (command == "problem") {
        // testdata 含 list（只读），粗粒度归为修改类，代价仅为串行化
        return sub == "create" || sub == "edit" || sub == "delete"
            || sub == "submit" || sub == "import" || sub == "testdata";
    }
    if (command == "contest") {
        if (sub == "create" || sub == "delete" || sub == "import") return true;
        // contest problem <cid> <idx> submit ... （view 为只读）
        if (sub == "problem" && argc >= 6 && std::string(argv[5]) == "submit") return true;
        return false;
    }
    if (command == "submit") return sub == "rejudge";
    if (command == "article") return sub == "create" || sub == "delete";
    if (command == "displaylang") return sub == "switch" || sub == "delete" || sub == "pull";
    return false;
}

// 主函数
int main(int argc, char* argv[]) {
    // 控制台切到 UTF-8（本地化文案在 GBK 控制台下乱码）；退出时自动恢复
    clijudge::platform::initConsoleUtf8();

    // argv 从宽字符命令行重建为 UTF-8：不依赖 CRT 按 ANSI 代码页（中文系统 GBK）
    // 转换命令行，否则中文标题会以非法 UTF-8 进入 JSON 导致崩溃
#ifdef _WIN32
    {
        int wargc = 0;
        LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (wargv) {
            static std::vector<std::string> argStorage;
            static std::vector<char*> argPtrs;
            argStorage.reserve((size_t)wargc);
            for (int i = 0; i < wargc; i++) {
                int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
                std::string s(n > 0 ? (size_t)n : 0, '\0');
                if (n > 0) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &s[0], n, nullptr, nullptr);
                if (n > 0 && !s.empty()) s.pop_back();
                argStorage.push_back(std::move(s));
            }
            LocalFree(wargv);
            argPtrs.reserve(argStorage.size() + 1);
            for (auto& s : argStorage) argPtrs.push_back(&s[0]);
            argPtrs.push_back(nullptr);
            argc = wargc;
            argv = argPtrs.data();
        }
    }
#endif

    // 检查参数数量
    if (argc < 2) {
        showHelp();
        return 1;
    }

    std::string command = argv[1];
    std::string dataDir = getDataDir();

    // 确保数据目录存在
    if (!fs::exists(dataDir)) {
        fs::create_directories(dataDir);
    }

    // 旧版平铺数据迁移到分类子目录（幂等）
    clijudge::platform::migrateFlatLayout(dataDir);

    // 语言配置检查（displaylang 和 help 命令不需要检查）
    if (command != "displaylang" && command != "help") {
        if (!clijudge::lang::isLangConfigured()) {
            clijudge::lang::showNoLangError();
            return 1;
        }
    }

    // 修改类命令获取全局数据锁（跨进程互斥，进程退出由 OS 自动释放）
    static clijudge::platform::DataLock dataLock;
    if (isMutationCommand(command, argc, argv)
        && !dataLock.lock(clijudge::platform::pathJoin(dataDir, ".clijudge.lock"))) {
        std::cerr << clijudge::lang::trf("err.data_lock", "Error: failed to acquire data lock at {0}", {dataDir}) << std::endl;
        return 1;
    }

    // 帮助命令
    if (command == "help") {
        if (argc >= 3) {
            showCommandHelp(argv[2]);
        } else {
            showHelp();
        }
        return 0;
    }

    // 文章管理
    if (command == "article") {
        if (argc < 3) {
            showCommandHelp("article");
            return 1;
        }

        std::string subCmd = argv[2];
        if (subCmd == "help") {
            showCommandHelp("article");
            return 0;
        } else if (subCmd == "count") {
            return clijudge::article::cmdCount(dataDir);
        } else if (subCmd == "create") {
            // "help" 不是合法标题: article create help/-h/--help 输出用法而非建文
            if (argc < 4 ||
                strcmp(argv[3], "help") == 0 || strcmp(argv[3], "-h") == 0 ||
                strcmp(argv[3], "--help") == 0) {
                std::cerr << clijudge::lang::tr("usage.article_create", "Usage: clijudge.exe article create [title] [md_file]") << std::endl;
                return 1;
            }
            std::string title = argv[3];
            std::string mdFile = (argc >= 5) ? argv[4] : "";
            return clijudge::article::cmdCreate(dataDir, title, mdFile);
        } else if (subCmd == "delete") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.article_delete", "Usage: clijudge.exe article delete [id]") << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return clijudge::article::cmdDelete(dataDir, id);
        } else if (subCmd == "list") {
            int L = (argc >= 4) ? parseInt(argv[3], 1) : 1;
            int R = (argc >= 5) ? parseInt(argv[4], 50) : 50;
            return clijudge::article::cmdList(dataDir, L, R);
        } else if (subCmd == "view") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.article_view", "Usage: clijudge.exe article view [id]") << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return clijudge::article::cmdView(dataDir, id);
        } else {
            std::cerr << clijudge::lang::trf("err.unknown_article_command", "Unknown article command: {0}", {subCmd}) << std::endl;
            showCommandHelp("article");
            return 1;
        }
    }

    // 比赛管理
    if (command == "contest") {
        if (argc < 3) {
            showCommandHelp("contest");
            return 1;
        }

        std::string subCmd = argv[2];
        if (subCmd == "help") {
            showCommandHelp("contest");
            return 0;
        } else if (subCmd == "create") {
            // "help" 不是合法标题: contest create help/-h/--help 输出用法而非建赛
            if (argc < 7 ||
                strcmp(argv[3], "help") == 0 || strcmp(argv[3], "-h") == 0 ||
                strcmp(argv[3], "--help") == 0) {
                std::cerr << clijudge::lang::tr("usage.contest_create", "Usage: clijudge.exe contest create [title] [start] [end] [prob1] [prob2]") << std::endl;
                return 1;
            }
            std::string title = argv[3];
            std::string startTime = argv[4];
            std::string endTime = argv[5];
            std::vector<int> problemIds;
            for (int i = 6; i < argc; i++) {
                problemIds.push_back(parseInt(argv[i]));
            }
            return clijudge::contest::cmdCreate(dataDir, title, startTime, endTime, problemIds);
        } else if (subCmd == "delete") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.contest_delete", "Usage: clijudge.exe contest delete [id]") << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return clijudge::contest::cmdDelete(dataDir, id);
        } else if (subCmd == "problem") {
            if (argc < 5) {
                std::cerr << clijudge::lang::tr("usage.contest_problem", "Usage: clijudge.exe contest problem [id] [prob_index] [submit|view]") << std::endl;
                return 1;
            }
            int contestId = parseInt(argv[3]);
            int probIndex = parseInt(argv[4]);
            if (argc >= 6) {
                std::string action = argv[5];
                if (action == "submit") {
                    if (argc < 7) {
                        std::cerr << clijudge::lang::tr("usage.contest_problem_submit", "Usage: clijudge.exe contest problem [id] [prob_index] submit [file] [--as username]") << std::endl;
                        return 1;
                    }
                    std::string filePath = argv[6];
                    std::string username;
                    for (int i = 7; i < argc; i++) {
                        if (strcmp(argv[i], "--as") == 0 && i + 1 < argc) {
                            username = argv[++i];
                        }
                    }
                    return clijudge::contest::cmdProblemSubmit(dataDir, contestId, probIndex, filePath, username);
                } else if (action == "view") {
                    return clijudge::contest::cmdProblemView(dataDir, contestId, probIndex);
                }
            }
            std::cerr << clijudge::lang::tr("err.unknown_contest_problem_action", "Unknown contest problem action.") << std::endl;
            return 1;
        } else if (subCmd == "view") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.contest_view", "Usage: clijudge.exe contest view [id]") << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return clijudge::contest::cmdView(dataDir, id);
        } else if (subCmd == "list") {
            return clijudge::contest::cmdList(dataDir);
        } else if (subCmd == "leaderboard") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.contest_leaderboard", "Usage: clijudge.exe contest leaderboard [id]") << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return clijudge::contest::cmdLeaderboard(dataDir, id);
        } else if (subCmd == "report") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.contest_report", "Usage: clijudge.exe contest report [id] [out_html]") << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            std::string outPath = (argc > 4) ? argv[4] : "";
            return clijudge::contest::cmdReport(dataDir, id, outPath);
        } else if (subCmd == "import") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.contest_import", "Usage: clijudge.exe contest import [cdf_path]") << std::endl;
                return 1;
            }
            std::string cdfPath = argv[3];
            return clijudge::contest::cmdImportCdf(dataDir, cdfPath);
        } else if (subCmd == "export") {
            if (argc < 5) {
                std::cerr << clijudge::lang::tr("usage.contest_export", "Usage: clijudge.exe contest export [id] [cdf_path]") << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            std::string cdfPath = argv[4];
            return clijudge::contest::cmdExportCdf(dataDir, id, cdfPath);
        } else {
            std::cerr << clijudge::lang::trf("err.unknown_contest_command", "Unknown contest command: {0}", {subCmd}) << std::endl;
            showCommandHelp("contest");
            return 1;
        }
    }

    // IDE
    if (command == "ide") {
        if (argc < 3) {
            showCommandHelp("ide");
            return 1;
        }

        std::string subCmd = argv[2];
        if (subCmd == "help") {
            showCommandHelp("ide");
            return 0;
        } else if (subCmd == "run") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.ide_run", "Usage: clijudge.exe ide run [code] [input]") << std::endl;
                return 1;
            }
            std::string codePath = argv[3];
            std::string inputPath = (argc >= 5) ? argv[4] : "";
            return clijudge::ide::cmdRun(codePath, inputPath);
        } else {
            std::cerr << clijudge::lang::trf("err.unknown_ide_command", "Unknown ide command: {0}", {subCmd}) << std::endl;
            showCommandHelp("ide");
            return 1;
        }
    }

    // 题目管理
    if (command == "problem") {
        if (argc < 3) {
            showCommandHelp("problem");
            return 1;
        }

        std::string subCmd = argv[2];
        if (subCmd == "help") {
            showCommandHelp("problem");
            return 0;
        } else if (subCmd == "count") {
            return clijudge::problem::cmdCount(dataDir);
        } else if (subCmd == "create") {
            // "help" 不是合法题目名: problem create help/-h/--help 输出用法而非建题
            if (argc < 4 ||
                strcmp(argv[3], "help") == 0 || strcmp(argv[3], "-h") == 0 ||
                strcmp(argv[3], "--help") == 0) {
                std::cerr << clijudge::lang::tr("usage.problem_create", "Usage: clijudge.exe problem create [title] [-background md] [-describe md] [-exampleio in out] [-instyle md] [-outstyle md] [-compare mode] [-spj-code md] [-spj-exe path] [-float-abs tol] [-float-rel tol] [-type type] [-subtask-mode mode] [-answer-ext ext] [-source-name name] [-dependence json] [-interactor file] [-grader dir] [-generator file] [-generator-exe path]") << std::endl;
                return 1;
            }
            std::string title = argv[3];
            std::string background, describe, exampleIn, exampleOut, instyle, outstyle;
            std::string compareMode, spjCode, spjExe;
            std::string problemType, answerExt, sourceName, subtaskMode;
            std::string dependenceJson, interactorFile, graderDir;
            std::string generatorFile, generatorExe;
            double floatAbsTol = 0.0, floatRelTol = 0.0;

            // 解析可选参数
            for (int i = 4; i < argc; i++) {
                if (strcmp(argv[i], "-background") == 0 && i + 1 < argc) {
                    background = argv[++i];
                } else if (strcmp(argv[i], "-describe") == 0 && i + 1 < argc) {
                    describe = argv[++i];
                } else if (strcmp(argv[i], "-exampleio") == 0 && i + 2 < argc) {
                    exampleIn = argv[++i];
                    exampleOut = argv[++i];
                } else if (strcmp(argv[i], "-instyle") == 0 && i + 1 < argc) {
                    instyle = argv[++i];
                } else if (strcmp(argv[i], "-outstyle") == 0 && i + 1 < argc) {
                    outstyle = argv[++i];
                } else if (strcmp(argv[i], "-compare") == 0 && i + 1 < argc) {
                    compareMode = argv[++i];
                } else if (strcmp(argv[i], "-spj-code") == 0 && i + 1 < argc) {
                    spjCode = argv[++i];
                } else if (strcmp(argv[i], "-spj-exe") == 0 && i + 1 < argc) {
                    spjExe = argv[++i];
                } else if (strcmp(argv[i], "-float-abs") == 0 && i + 1 < argc) {
                    floatAbsTol = std::stod(argv[++i]);
                } else if (strcmp(argv[i], "-float-rel") == 0 && i + 1 < argc) {
                    floatRelTol = std::stod(argv[++i]);
                } else if (strcmp(argv[i], "-type") == 0 && i + 1 < argc) {
                    problemType = argv[++i];
                } else if (strcmp(argv[i], "-subtask-mode") == 0 && i + 1 < argc) {
                    subtaskMode = argv[++i];
                } else if (strcmp(argv[i], "-answer-ext") == 0 && i + 1 < argc) {
                    answerExt = argv[++i];
                } else if (strcmp(argv[i], "-source-name") == 0 && i + 1 < argc) {
                    sourceName = argv[++i];
                } else if (strcmp(argv[i], "-dependence") == 0 && i + 1 < argc) {
                    dependenceJson = argv[++i];
                } else if (strcmp(argv[i], "-interactor") == 0 && i + 1 < argc) {
                    interactorFile = argv[++i];
                } else if (strcmp(argv[i], "-grader") == 0 && i + 1 < argc) {
                    graderDir = argv[++i];
                } else if (strcmp(argv[i], "-generator") == 0 && i + 1 < argc) {
                    generatorFile = argv[++i];
                } else if (strcmp(argv[i], "-generator-exe") == 0 && i + 1 < argc) {
                    generatorExe = argv[++i];
                }
            }

            return clijudge::problem::cmdCreate(dataDir, title, background, describe,
                                                  exampleIn, exampleOut, instyle, outstyle,
                                                  compareMode, spjCode, spjExe,
                                                  floatAbsTol, floatRelTol,
                                                  problemType, answerExt, sourceName,
                                                  subtaskMode, dependenceJson,
                                                  interactorFile, graderDir,
                                                  generatorFile, generatorExe);
        } else if (subCmd == "delete") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.problem_delete", "Usage: clijudge.exe problem delete [id]") << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return clijudge::problem::cmdDelete(dataDir, id);
        } else if (subCmd == "edit") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.problem_edit", "Usage: clijudge.exe problem edit [id] [-title title] [-background md] [-describe md] [-exampleio in out] [-instyle md] [-outstyle md] [-compare mode] [-spj-code md] [-spj-exe path] [-float-abs tol] [-float-rel tol] [-type type] [-subtask-mode mode] [-answer-ext ext] [-source-name name] [-dependence json] [-interactor file] [-grader dir] [-generator file] [-generator-exe path]") << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            std::string title, background, describe, exampleIn, exampleOut, instyle, outstyle;
            std::string compareMode, spjCode, spjExe;
            std::string problemType, answerExt, sourceName, subtaskMode;
            std::string dependenceJson, interactorFile, graderDir;
            std::string generatorFile, generatorExe;
            double floatAbsTol = 0.0, floatRelTol = 0.0;

            // 解析可选参数
            for (int i = 4; i < argc; i++) {
                if (strcmp(argv[i], "-title") == 0 && i + 1 < argc) {
                    title = argv[++i];
                } else if (strcmp(argv[i], "-background") == 0 && i + 1 < argc) {
                    background = argv[++i];
                } else if (strcmp(argv[i], "-describe") == 0 && i + 1 < argc) {
                    describe = argv[++i];
                } else if (strcmp(argv[i], "-exampleio") == 0 && i + 2 < argc) {
                    exampleIn = argv[++i];
                    exampleOut = argv[++i];
                } else if (strcmp(argv[i], "-instyle") == 0 && i + 1 < argc) {
                    instyle = argv[++i];
                } else if (strcmp(argv[i], "-outstyle") == 0 && i + 1 < argc) {
                    outstyle = argv[++i];
                } else if (strcmp(argv[i], "-compare") == 0 && i + 1 < argc) {
                    compareMode = argv[++i];
                } else if (strcmp(argv[i], "-spj-code") == 0 && i + 1 < argc) {
                    spjCode = argv[++i];
                } else if (strcmp(argv[i], "-spj-exe") == 0 && i + 1 < argc) {
                    spjExe = argv[++i];
                } else if (strcmp(argv[i], "-float-abs") == 0 && i + 1 < argc) {
                    floatAbsTol = std::stod(argv[++i]);
                } else if (strcmp(argv[i], "-float-rel") == 0 && i + 1 < argc) {
                    floatRelTol = std::stod(argv[++i]);
                } else if (strcmp(argv[i], "-type") == 0 && i + 1 < argc) {
                    problemType = argv[++i];
                } else if (strcmp(argv[i], "-subtask-mode") == 0 && i + 1 < argc) {
                    subtaskMode = argv[++i];
                } else if (strcmp(argv[i], "-answer-ext") == 0 && i + 1 < argc) {
                    answerExt = argv[++i];
                } else if (strcmp(argv[i], "-source-name") == 0 && i + 1 < argc) {
                    sourceName = argv[++i];
                } else if (strcmp(argv[i], "-dependence") == 0 && i + 1 < argc) {
                    dependenceJson = argv[++i];
                } else if (strcmp(argv[i], "-interactor") == 0 && i + 1 < argc) {
                    interactorFile = argv[++i];
                } else if (strcmp(argv[i], "-grader") == 0 && i + 1 < argc) {
                    graderDir = argv[++i];
                } else if (strcmp(argv[i], "-generator") == 0 && i + 1 < argc) {
                    generatorFile = argv[++i];
                } else if (strcmp(argv[i], "-generator-exe") == 0 && i + 1 < argc) {
                    generatorExe = argv[++i];
                }
            }

            return clijudge::problem::cmdEdit(dataDir, id, title, background, describe,
                                               exampleIn, exampleOut, instyle, outstyle,
                                               compareMode, spjCode, spjExe,
                                               floatAbsTol, floatRelTol,
                                               problemType, answerExt, sourceName,
                                               subtaskMode, dependenceJson,
                                               interactorFile, graderDir,
                                               generatorFile, generatorExe);
        } else if (subCmd == "view") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.problem_view", "Usage: clijudge.exe problem view [id]") << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return clijudge::problem::cmdView(dataDir, id);
        } else if (subCmd == "list") {
            int L = (argc >= 4) ? parseInt(argv[3], 1) : 1;
            int R = (argc >= 5) ? parseInt(argv[4], 50) : 50;
            return clijudge::problem::cmdList(dataDir, L, R);
        } else if (subCmd == "submit") {
            if (argc < 5) {
                std::cerr << clijudge::lang::tr("usage.problem_submit", "Usage: clijudge.exe problem submit [id] [file] [--as username]") << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            std::string filePath = argv[4];
            std::string username;
            for (int i = 5; i < argc; i++) {
                if (strcmp(argv[i], "--as") == 0 && i + 1 < argc) {
                    username = argv[++i];
                }
            }
            return clijudge::problem::cmdSubmit(dataDir, id, filePath, username);
        } else if (subCmd == "export") {
            if (argc < 5) {
                std::cerr << clijudge::lang::tr("usage.problem_export", "Usage: clijudge.exe problem export [id] [zip_path]") << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            std::string zipPath = argv[4];
            return clijudge::problem::cmdExport(dataDir, id, zipPath);
        } else if (subCmd == "import") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.problem_import", "Usage: clijudge.exe problem import [zip_path]") << std::endl;
                return 1;
            }
            std::string zipPath = argv[3];
            return clijudge::problem::cmdImport(dataDir, zipPath);
        } else if (subCmd == "testdata") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.problem_testdata", "Usage: clijudge.exe problem testdata [id] [command]") << std::endl;
                return 1;
            }
            int problemId = parseInt(argv[3]);
            if (argc < 5) {
                return clijudge::problem::cmdTestDataList(dataDir, problemId);
            }
            std::string tcCmd = argv[4];
            if (tcCmd == "list") {
                return clijudge::problem::cmdTestDataList(dataDir, problemId);
            } else if (tcCmd == "create") {
                if (argc < 7) {
                    std::cerr << clijudge::lang::tr("usage.problem_testdata_create", "Usage: clijudge.exe problem testdata [id] create [in] [out] [time] [mem] [pts] (in/out can be - placeholder for generator)") << std::endl;
                    return 1;
                }
                std::string inputData = argv[5];
                std::string outputData = argv[6];
                int timeLimit = (argc >= 8) ? parseInt(argv[7], -1) : -1;
                int memoryLimit = (argc >= 9) ? parseInt(argv[8], -1) : -1;
                int score = (argc >= 10) ? parseInt(argv[9], 25) : 25;
                return clijudge::problem::cmdTestDataCreate(dataDir, problemId, inputData, outputData, timeLimit, memoryLimit, score);
            } else if (tcCmd == "delete") {
                if (argc < 6) {
                    std::cerr << clijudge::lang::tr("usage.problem_testdata_delete", "Usage: clijudge.exe problem testdata [id] delete [tc_id]") << std::endl;
                    return 1;
                }
                int tcId = parseInt(argv[5]);
                return clijudge::problem::cmdTestDataDelete(dataDir, problemId, tcId);
            } else if (tcCmd == "-set-all") {
                int timeLimit = -1, memoryLimit = -1, score = -1;
                for (int i = 5; i < argc; i++) {
                    if (strcmp(argv[i], "-time") == 0 && i + 1 < argc) {
                        timeLimit = parseInt(argv[++i]);
                    } else if (strcmp(argv[i], "-mem") == 0 && i + 1 < argc) {
                        memoryLimit = parseInt(argv[++i]);
                    } else if (strcmp(argv[i], "-pts") == 0 && i + 1 < argc) {
                        score = parseInt(argv[++i]);
                    }
                }
                return clijudge::problem::cmdTestDataSetAll(dataDir, problemId, timeLimit, memoryLimit, score);
            } else if (tcCmd == "-zip") {
                if (argc < 6) {
                    std::cerr << clijudge::lang::tr("usage.problem_testdata_zip", "Usage: clijudge.exe problem testdata [id] -zip [zip_path]") << std::endl;
                    return 1;
                }
                std::string zipPath = argv[5];
                return clijudge::problem::cmdTestDataImportZip(dataDir, problemId, zipPath);
            } else {
                std::cerr << clijudge::lang::trf("err.unknown_testdata_command", "Unknown testdata command: {0}", {tcCmd}) << std::endl;
                return 1;
            }
        } else {
            std::cerr << clijudge::lang::trf("err.unknown_problem_command", "Unknown problem command: {0}", {subCmd}) << std::endl;
            showCommandHelp("problem");
            return 1;
        }
    }

    // 提交管理
    if (command == "submit") {
        if (argc < 3) {
            showCommandHelp("submit");
            return 1;
        }

        std::string subCmd = argv[2];
        if (subCmd == "help") {
            showCommandHelp("submit");
            return 0;
        } else if (subCmd == "count") {
            return clijudge::submit::cmdCount(dataDir);
        } else if (subCmd == "list") {
            int L = (argc >= 4) ? parseInt(argv[3], 1) : 1;
            int R = (argc >= 5) ? parseInt(argv[4], 50) : 50;
            return clijudge::submit::cmdList(dataDir, L, R);
        } else if (subCmd == "rejudge") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.submit_rejudge", "Usage: clijudge submit rejudge [id]") << std::endl;
                return 1;
            }
            int id = parseInt(argv[3]);
            return clijudge::problem::cmdRejudge(dataDir, id);
        } else {
            std::cerr << clijudge::lang::trf("err.unknown_submit_command", "Unknown submit command: {0}", {subCmd}) << std::endl;
            showCommandHelp("submit");
            return 1;
        }
    }

    // 语言管理
    if (command == "displaylang") {
        if (argc < 3) {
            showCommandHelp("displaylang");
            return 1;
        }

        std::string subCmd = argv[2];
        if (subCmd == "help") {
            showCommandHelp("displaylang");
            return 0;
        } else if (subCmd == "list") {
            bool online = (argc >= 4 && std::string(argv[3]) == "--online");
            return clijudge::lang::cmdList(online);
        } else if (subCmd == "switch") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.displaylang_switch", "Usage: clijudge displaylang switch [langname]") << std::endl;
                return 1;
            }
            return clijudge::lang::cmdSwitch(argv[3]);
        } else if (subCmd == "delete") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.displaylang_delete", "Usage: clijudge displaylang delete [langname]") << std::endl;
                return 1;
            }
            return clijudge::lang::cmdDelete(argv[3]);
        } else if (subCmd == "pull") {
            if (argc < 4) {
                std::cerr << clijudge::lang::tr("usage.displaylang_pull", "Usage: clijudge displaylang pull [langname]") << std::endl;
                return 1;
            }
            return clijudge::lang::cmdPull(argv[3]);
        } else if (subCmd == "source") {
            return clijudge::lang::cmdSource(argc >= 4 ? argv[3] : "", argc >= 5 ? argv[4] : "");
        } else {
            std::cerr << clijudge::lang::trf("err.unknown_displaylang_command", "Unknown displaylang command: {0}", {subCmd}) << std::endl;
            showCommandHelp("displaylang");
            return 1;
        }
    }

    // 未知命令
    std::cerr << clijudge::lang::trf("err.unknown_command", "Unknown command: {0}", {command}) << std::endl;
    showHelp();
    return 1;
}