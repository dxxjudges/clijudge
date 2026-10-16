#ifndef CLIJUDGE_SETTINGS_H
#define CLIJUDGE_SETTINGS_H

// settings.h
// 评测配置模块 —— 对应 LemonLime Settings 的评测相关设置
//
// 存储位置: data/config.json 的 "judge" 段（与 current_lang 等共存，互不覆盖）
//   {
//     "current_lang": "en",
//     "judge": {
//       "compile_time_limit_ms": 10000,
//       "special_judge_time_limit_ms": 10000,
//       "source_size_limit_kb": 50,
//       "rejudge_times": 1,
//       "max_rejudge_times": 12,
//       "max_judging_threads": 4,
//       "extra_time_ratio": 0.1,
//       "file_write_limit_kb": 16384,
//       "env": { "KEY": "VALUE" },
//       "custom_languages": {
//         "judgelang": { "extensions": [".jlang", ".judgelang"], "compile": "", "run": "judgelang {src}" }
//       }
//     }
//   }
// custom_languages 键缺失时自动内置 judgelang 示例语言;
// 自定义语言优先级高于内置: 注册与内置相同扩展名 (如 .py/.cpp) 时覆盖内置处理;
// compile 为空 = 脚本语言, 模板占位符 {src}/{exe}/{dir}, 见 CustomLanguage 注释。

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include "json.hpp"
#include "platform.h"
#include "lang.h"

namespace clijudge {
namespace settings {

using json = nlohmann::json;
namespace fs = std::filesystem;

// 配置文件路径（与 lang.h getConfigPath 保持一致：统一走 platform::dataDir）
inline std::string configPath() {
    return platform::pathJoin(platform::dataDir(), "config.json");
}

// ── 自定义评测语言 ─────────────────────────────────────────
// 按扩展名注册可评测语言 (config.json judge.custom_languages); 优先级高于内置
// 模板占位符:
//   {src} - 选手源文件绝对路径 (多个源文件时独立成参)
//   {exe} - 编译产物路径 (<workDir>/program<exeSuffix>)
//   {dir} - 编译工作目录绝对路径
// compile 为空 → 脚本语言, 直接执行 run;
// 命令按空白分词 (支持双引号包裹含空白的片段), 参数直传沙箱, 不经 shell.
struct CustomLanguage {
    std::string name;
    std::vector<std::string> extensions;  // 小写、含点 (".cpp")
    std::string compile;
    std::string run;
};

// 内置示例语言 judgelang: .jlang 源文件交由 PATH 上的 judgelang 解释器执行
inline CustomLanguage defaultJudgelang() {
    CustomLanguage c;
    c.name = "judgelang";
    c.extensions = {".jlang", ".judgelang"};
    c.run = "judgelang {src}";
    return c;
}

inline std::string normalizeExt(std::string ext) {
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return (char)std::tolower(ch); });
    if (!ext.empty() && ext[0] != '.') ext = "." + ext;
    return ext;
}

// 命令模板分词: 空白分隔, 双引号内保留空白
inline std::vector<std::string> tokenizeCommand(const std::string& tpl) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false;
    for (char c : tpl) {
        if (c == '"') { inQuote = !inQuote; continue; }
        if (!inQuote && (c == ' ' || c == '\t')) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// 展开模板占位符 → argv (sources 会先转为绝对路径)
inline std::vector<std::string> expandCommand(const std::vector<std::string>& tokens,
                                              const std::vector<std::string>& sources,
                                              const std::string& exePath,
                                              const std::string& workDir) {
    auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
        if (from.empty()) return;
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    std::vector<std::string> absSources;
    absSources.reserve(sources.size());
    for (const auto& s : sources) {
        std::error_code ec;
        fs::path p = fs::absolute(s, ec);
        absSources.push_back(ec ? s : p.string());
    }

    std::vector<std::string> out;
    for (auto tok : tokens) {
        if (tok == "{src}") {
            for (const auto& s : absSources) out.push_back(s);
            continue;
        }
        replaceAll(tok, "{exe}", exePath);
        replaceAll(tok, "{dir}", workDir);
        if (!absSources.empty()) replaceAll(tok, "{src}", absSources.front());
        out.push_back(tok);
    }
    return out;
}

// 评测设置
struct JudgeSettings {
    int compileTimeLimitMs = 10000;          // 编译超时 (LemonLime getCompileTimeLimit)
    int specialJudgeTimeLimitMs = 10000;     // 特殊评测/交互进程超时
    int sourceSizeLimitKB = 50;              // 源代码大小上限 (fileSizeLimit * 1024 字节)
    int rejudgeTimes = 1;                    // 边界 TLE 自动重评测次数 (0..12)
    int maxRejudgeTimes = 12;                // 手动 rejudge 允许的最大评测次数
    int maxJudgingThreads = 4;               // 并行评测线程数
    double extraTimeRatio = 0.1;             // 额外时间比 (defaultExtraTimeRatio)
    long long fileWriteLimitKB = 16384;      // 子进程写文件大小上限 (RLIMIT_FSIZE)
    std::map<std::string, std::string> env;  // 追加到子进程的环境变量
    // 自定义评测语言 (键缺失时默认内置 judgelang; 显式给出则以配置为准)
    std::vector<CustomLanguage> customLanguages = {defaultJudgelang()};
};

inline int clampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// 类型安全取值: 字段存在但类型不符 (如 "compile_time_limit_ms": "10000")
// 时回退默认值, 而不是让 json::value() 抛 type_error 直接 abort 评测命令
template <typename T>
inline T jsonValue(const json& j, const char* key, T def) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    try {
        return it->get<T>();
    } catch (...) {
        return def;
    }
}

inline JudgeSettings fromJson(const json& j) {
    JudgeSettings s;
    if (!j.is_object()) return s;
    s.compileTimeLimitMs = clampInt(jsonValue<int>(j, "compile_time_limit_ms", s.compileTimeLimitMs), 100, 300000);
    s.specialJudgeTimeLimitMs = clampInt(jsonValue<int>(j, "special_judge_time_limit_ms", s.specialJudgeTimeLimitMs), 100, 300000);
    s.sourceSizeLimitKB = clampInt(jsonValue<int>(j, "source_size_limit_kb", s.sourceSizeLimitKB), 1, 1024 * 1024);
    s.rejudgeTimes = clampInt(jsonValue<int>(j, "rejudge_times", s.rejudgeTimes), 0, 12);
    s.maxRejudgeTimes = clampInt(jsonValue<int>(j, "max_rejudge_times", s.maxRejudgeTimes), 0, 100000);
    s.maxJudgingThreads = clampInt(jsonValue<int>(j, "max_judging_threads", s.maxJudgingThreads), 1, 64);
    double r = jsonValue<double>(j, "extra_time_ratio", s.extraTimeRatio);
    if (r < 0.0) r = 0.0;
    if (r > 10.0) r = 10.0;
    s.extraTimeRatio = r;
    long long fw = jsonValue<long long>(j, "file_write_limit_kb", (long long)s.fileWriteLimitKB);
    if (fw < 64) fw = 64;
    s.fileWriteLimitKB = fw;
    if (j.contains("env") && j["env"].is_object()) {
        for (auto it = j["env"].begin(); it != j["env"].end(); ++it) {
            if (it.value().is_string()) s.env[it.key()] = it.value().get<std::string>();
        }
    }
    if (j.contains("custom_languages") && j["custom_languages"].is_object()) {
        s.customLanguages.clear();
        for (auto it = j["custom_languages"].begin(); it != j["custom_languages"].end(); ++it) {
            const json& v = it.value();
            if (!v.is_object()) continue;
            CustomLanguage c;
            c.name = it.key();
            if (v.contains("extensions") && v["extensions"].is_array()) {
                for (const auto& e : v["extensions"]) {
                    if (e.is_string()) {
                        std::string ne = normalizeExt(e.get<std::string>());
                        if (!ne.empty() && ne != ".") c.extensions.push_back(ne);
                    }
                }
            }
            c.compile = jsonValue<std::string>(v, "compile", "");
            c.run = jsonValue<std::string>(v, "run", "");
            if (c.extensions.empty() || c.run.empty()) continue;  // 无效条目跳过
            s.customLanguages.push_back(std::move(c));
        }
    }
    return s;
}

inline json toJson(const JudgeSettings& s) {
    json j;
    j["compile_time_limit_ms"] = s.compileTimeLimitMs;
    j["special_judge_time_limit_ms"] = s.specialJudgeTimeLimitMs;
    j["source_size_limit_kb"] = s.sourceSizeLimitKB;
    j["rejudge_times"] = s.rejudgeTimes;
    j["max_rejudge_times"] = s.maxRejudgeTimes;
    j["max_judging_threads"] = s.maxJudgingThreads;
    j["extra_time_ratio"] = s.extraTimeRatio;
    j["file_write_limit_kb"] = s.fileWriteLimitKB;
    json env = json::object();
    for (const auto& [k, v] : s.env) env[k] = v;
    j["env"] = env;
    json cl = json::object();
    for (const auto& c : s.customLanguages) {
        json e;
        e["extensions"] = c.extensions;
        e["compile"] = c.compile;
        e["run"] = c.run;
        cl[c.name] = e;
    }
    j["custom_languages"] = cl;
    return j;
}

// 读取整个 config.json（容忍不存在；损坏/非对象时回退默认并告警，避免静默忽略用户配置）
inline json loadRawConfig() {
    std::ifstream f(configPath());
    if (!f.is_open()) return json::object();
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // 记事本另存的 UTF-8 BOM 会让 json 解析失败 → 剥掉后再解析
    if (content.size() >= 3 && (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF)
        content = content.substr(3);
    if (content.empty()) return json::object();
    try {
        json j = json::parse(content);
        if (j.is_object()) return j;
        std::cerr << clijudge::lang::trf("err.config_not_object", "Warning: {0} is not a JSON object; using defaults.", {configPath()}) << std::endl;
    } catch (const std::exception& e) {
        std::cerr << clijudge::lang::trf("err.config_parse_failed", "Warning: failed to parse {0} ({1}); using defaults.",
                                         {configPath(), e.what()}) << std::endl;
    }
    return json::object();
}

// 读取评测设置
inline JudgeSettings load() {
    json raw = loadRawConfig();
    if (raw.contains("judge")) return fromJson(raw["judge"]);
    return JudgeSettings{};
}

// 保存评测设置（保留 config.json 其它键）
inline bool save(const JudgeSettings& s) {
    json raw = loadRawConfig();
    raw["judge"] = toJson(s);
    try {
        fs::create_directories(fs::path(configPath()).parent_path());
        return platform::writeFileAtomic(configPath(), raw.dump(2));
    } catch (...) {
        return false;
    }
}

// 获取当前设置（进程内缓存）
inline const JudgeSettings& get() {
    static JudgeSettings s = load();
    return s;
}

// ── 便捷 getter（对应 LemonLime Settings::get*）──────────────
inline int getCompileTimeLimit()          { return get().compileTimeLimitMs; }
inline int getSpecialJudgeTimeLimit()     { return get().specialJudgeTimeLimitMs; }
inline int getFileSizeLimitKB()           { return get().sourceSizeLimitKB; }
inline int getRejudgeTimes()              { return get().rejudgeTimes; }
inline int getMaxRejudgeTimes()           { return get().maxRejudgeTimes; }
inline int getMaxJudgingThreads()         { return get().maxJudgingThreads; }
inline double getDefaultExtraTimeRatio()  { return get().extraTimeRatio; }
inline long long getFileWriteLimitBytes() { return get().fileWriteLimitKB * 1024; }
inline const std::map<std::string, std::string>& getExtraEnv() { return get().env; }

// 按扩展名查找自定义语言 (ext 需为小写、含点); 未注册返回 nullptr
inline const CustomLanguage* findByExtension(const std::string& ext) {
    for (const auto& c : get().customLanguages) {
        for (const auto& e : c.extensions) {
            if (e == ext) return &c;
        }
    }
    return nullptr;
}

// 评测设置转为子进程环境变量列表 ("K=V")
inline std::vector<std::string> extraEnvList() {
    std::vector<std::string> out;
    for (const auto& [k, v] : get().env) out.push_back(k + "=" + v);
    return out;
}

} // namespace settings
} // namespace clijudge

#endif // CLIJUDGE_SETTINGS_H
