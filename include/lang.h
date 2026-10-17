#ifndef CLIJUDGE_LANG_H
#define CLIJUDGE_LANG_H

// lang.h
// CliJudge 语言管理模块
//
// 子命令:
//   list                  显示本地已有语言
//   list --online         显示仓库中可用的语言
//   switch [langname]     切换语言（若本地没有则下载）
//   delete [langname]     删除本地语言文件
//   pull [langname]       拉取语言文件（不切换）

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <winhttp.h>
#ifdef _MSC_VER
#pragma comment(lib, "winhttp.lib")
#endif
#endif
#include "json.hpp"
#include "platform.h"

namespace clijudge {
namespace lang {

using json = nlohmann::json;
namespace fs = std::filesystem;

// trf 前向声明: loadConfig/downloadLang 等早期函数先于定义使用它
inline std::string trf(const std::string& key, const std::string& fallback,
                       const std::vector<std::string>& args);

// 仓库信息
const std::string REPO_OWNER = "dxxjudges";
const std::string REPO_NAME = "clijudge";
const std::string LANGS_BRANCH = "languages";
const std::string GITEE_OWNER = "dxx114514"; // 语言包镜像仓库所有者（Gitee）

// 获取 exe 所在目录
inline std::string getExeDir() {
    return platform::exeDir();
}

// 数据目录（统一走 platform::dataDir：环境变量优先，其次 exe 目录/data）
inline std::string dataDir() {
    return platform::dataDir();
}

// 获取语言文件目录
inline std::string getLangsDir() {
    return platform::pathJoin(dataDir(), "langs");
}

// 获取配置文件路径
inline std::string getConfigPath() {
    return platform::pathJoin(dataDir(), "config.json");
}

// 语言名白名单（防路径穿越与 URL/shell 注入）
inline bool validLangName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    if (name == "." || name == "..") return false;
    for (char c : name) {
        if (!isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') return false;
    }
    return true;
}

// 确保目录存在
inline void ensureDir(const std::string& dir) {
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
}

// UTF-16（按 BOM 指定的字节序）转 UTF-8（字节级实现，跨平台一致）
inline std::string utf16ToUtf8(const char* data, size_t bytes, bool bigEndian) {
    auto unit = [&](size_t idx) -> uint16_t {
        unsigned char a = (unsigned char)data[idx];
        unsigned char b = (unsigned char)data[idx + 1];
        return bigEndian ? (uint16_t)((a << 8) | b) : (uint16_t)((b << 8) | a);
    };
    std::string out;
    size_t i = 0;
    while (i + 1 < bytes) {
        uint16_t u = unit(i);
        i += 2;
        uint32_t cp;
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 1 >= bytes) break;
            uint16_t lo = unit(i);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000u + ((((uint32_t)u - 0xD800u) << 10) | ((uint32_t)lo - 0xDC00u));
                i += 2;
            } else {
                cp = 0xFFFD;
            }
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            cp = 0xFFFD;
        } else {
            cp = u;
        }
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

// 读取文件内容并转换为 UTF-8
inline std::string readFile(const std::string& path) {
    if (!fs::exists(path)) return "";
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (raw.empty()) return "";
    
    // 检测 BOM
    if (raw.size() >= 3 &&
        (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF) {
        // UTF-8 BOM, 去掉前3字节
        return raw.substr(3);
    }
    if (raw.size() >= 2 &&
        (unsigned char)raw[0] == 0xFF &&
        (unsigned char)raw[1] == 0xFE) {
        // UTF-16 LE BOM, 转换为 UTF-8
        return utf16ToUtf8(raw.data() + 2, raw.size() - 2, false);
    }
    if (raw.size() >= 2 &&
        (unsigned char)raw[0] == 0xFE &&
        (unsigned char)raw[1] == 0xFF) {
        // UTF-16 BE BOM, 转换为 UTF-8
        return utf16ToUtf8(raw.data() + 2, raw.size() - 2, true);
    }
    // 无 BOM, 按 UTF-8 返回
    return raw;
}

// 写入文件内容
inline bool writeFile(const std::string& path, const std::string& content) {
    ensureDir(fs::path(path).parent_path().string());
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(content.data(), content.size());
    return f.good();
}

// 内置英文语言包（离线兜底；与 languages 分支 langs/en.cjl 保持同步）
inline const std::string& builtinEnJson() {
    static const std::string kEn = R"CLJ({
  "meta": {
    "name": "en",
    "display": "English",
    "version": "1.0.0"
  },
  "strings": {
    "help": {
      "title": "CliJudge - Lightweight Command-Line Judge System",
      "usage": "Usage: clijudge <command> [arguments...]",
      "commands": "Commands:",
      "article": "Article management",
      "contest": "Contest management",
      "ide": "IDE functions",
      "problem": "Problem management",
      "submit": "Submission management",
      "displaylang": "Display language settings",
      "more_info": "Use 'clijudge <command> help' for more information about a command."
    },
    "article": {
      "commands": "Article Commands:",
      "count": "Count articles",
      "create": "Create article",
      "delete": "Delete article",
      "list": "List articles",
      "view": "View article"
    },
    "contest": {
      "commands": "Contest Commands:",
      "create": "Create contest",
      "delete": "Delete contest",
      "export": "Export contest to CDF",
      "import": "Import contest from CDF",
      "leaderboard": "View contest leaderboard",
      "report": "Export contest report (HTML)",
      "problem": "Contest problem",
      "submit": "Submit solution",
      "view_submissions": "View submissions",
      "view": "View contest"
    },
    "problem": {
      "commands": "Problem Commands:",
      "count": "Count problems",
      "create": "Create problem",
      "delete": "Delete problem",
      "edit": "Edit problem (same options as create)",
      "export": "Export problem",
      "import": "Import problem",
      "list": "List problems",
      "submit": "Submit solution",
      "testdata": "Test data management",
      "set_all": "Set all test data defaults",
      "import_zip": "Import test data from zip",
      "create_test": "Create test case",
      "delete_test": "Delete test case",
      "list_test": "List test cases",
      "view": "View problem"
    },
    "submit": {
      "commands": "Submit Commands:",
      "count": "Count submissions",
      "list": "List submissions",
      "rejudge": "Rejudge submission"
    },
    "ide": {
      "commands": "IDE Commands:",
      "run": "Run code"
    },
    "displaylang": {
      "commands": "Display Language Commands:",
      "list": "List local languages",
      "list_online": "List online languages",
      "switch": "Switch display language",
      "delete": "Delete local language",
      "pull": "Pull language (no switch)"
    },
    "judge": {
      "accepted": "Accepted",
      "wrong_answer": "Wrong Answer",
      "time_limit": "Time Limit Exceeded",
      "memory_limit": "Memory Limit Exceeded",
      "runtime_error": "Runtime Error",
      "compile_error": "Compilation Error",
      "system_error": "System Error",
      "skipped": "Skipped",
      "presentation_error": "Presentation Error",
      "output_limit": "Output Limit Exceeded",
      "partially_correct": "Partially Correct",
      "invalid_spj": "Invalid Special Judge",
      "special_judge_tle": "Special Judge Time Limit Exceeded",
      "special_judge_runtime": "Special Judge Runtime Error",
      "interactor_error": "Interactor Error",
      "score": "Score",
      "time": "Time",
      "memory": "Memory",
      "status": "Status",
      "user": "User"
    },
    "error": {
      "no_lang": "Error: No display language configured.",
      "no_lang_hint": "Please run: clijudge displaylang switch [langname]",
      "no_lang_list": "Use 'clijudge displaylang list --online' to see available languages.",
      "not_found": "not found",
      "failed": "Failed",
      "usage": "Usage:"
    },
    "success": {
      "created": "created",
      "deleted": "deleted",
      "updated": "updated",
      "switched": "Switched to language",
      "downloaded": "Downloaded successfully"
    }
  }
}
)CLJ";
    return kEn;
}

// 读取配置
inline json loadConfig() {
    std::string path = getConfigPath();
    std::string content = readFile(path);
    if (content.empty()) {
        return json{{"current_lang", ""}};
    }
    // 记事本另存的 UTF-8 BOM 会让 json::parse 失败 → 剥掉后再解析,
    // 否则后续 saveConfig 会把整个文件 (含 judge 段) 覆写成只剩 current_lang
    if (content.size() >= 3 && (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF)
        content = content.substr(3);
    try {
        json j = json::parse(content);
        if (j.is_object()) return j;
        static bool warnedType = false;
        if (!warnedType) {
            warnedType = true;
            std::cerr << trf("err.config_not_object", "Warning: {0} is not a JSON object; using defaults.", {path}) << std::endl;
        }
    } catch (...) {
        // 损坏的 config.json 静默回退会让用户困惑，明确告警一次
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::cerr << trf("err.config_invalid_json", "Warning: failed to parse {0} (invalid JSON); using defaults.", {path}) << std::endl;
        }
    }
    return json{{"current_lang", ""}};
}

// 保存配置（原子写，防崩溃留下半截 config.json）
inline bool saveConfig(const json& config) {
    return platform::writeFileAtomic(getConfigPath(), config.dump(2));
}

// 首次运行（配置文件不存在）自动启用内置英文语言包，保证离线可用
inline void ensureDefaultLang() {
    std::string cfgPath = getConfigPath();
    if (fs::exists(cfgPath)) return;
    std::string langPath = platform::pathJoin(getLangsDir(), "en.cjl");
    if (!fs::exists(langPath)) {
        writeFile(langPath, builtinEnJson());
    }
    saveConfig(json{{"current_lang", "en"}});
}

// 获取当前语言名
inline std::string getCurrentLang() {
    json config = loadConfig();
    return config.value("current_lang", "");
}

// 语言包源（github/gitee/custom），displaylang source 命令可切换
inline std::string langSource() {
    std::string s = loadConfig().value("lang_source", "github");
    if (s == "gitee" || s == "custom") return s;
    return "github";
}

// 自定义源的 raw 基址（保存时已规范化为以 / 结尾）；无有效值返回空
inline std::string langSourceCustomRaw() {
    std::string u = loadConfig().value("lang_source_url", "");
    if (!u.empty() && (u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0))
        return u;
    return "";
}

// 当前源的语言文件 raw 下载基址（末尾含 /）
inline std::string langSourceRawBase() {
    std::string s = langSource();
    if (s == "gitee")
        return "https://gitee.com/" + GITEE_OWNER + "/" + REPO_NAME +
               "/raw/" + LANGS_BRANCH + "/langs/";
    if (s == "custom") {
        std::string u = langSourceCustomRaw();
        if (!u.empty()) return u;
    }
    return "https://raw.githubusercontent.com/" + REPO_OWNER + "/" + REPO_NAME +
           "/" + LANGS_BRANCH + "/langs/";
}

// 由 raw 基址推导在线列表 API（仅识别 github/gitee 布局，推不出返回空）
inline std::string deriveApiUrl(const std::string& rawBase) {
    // https://raw.githubusercontent.com/{owner}/{repo}/{branch}/langs/
    const std::string gh = "https://raw.githubusercontent.com/";
    if (rawBase.rfind(gh, 0) == 0) {
        std::string rest = rawBase.substr(gh.size());
        size_t a = rest.find('/');
        size_t b = a == std::string::npos ? std::string::npos : rest.find('/', a + 1);
        size_t c = b == std::string::npos ? std::string::npos : rest.find('/', b + 1);
        if (a == std::string::npos || b == std::string::npos || c == std::string::npos)
            return "";
        return "https://api.github.com/repos/" + rest.substr(0, b) +
               "/contents/langs?ref=" + rest.substr(b + 1, c - b - 1);
    }
    // https://gitee.com/{owner}/{repo}/raw/{branch}/langs/
    const std::string ge = "https://gitee.com/";
    if (rawBase.rfind(ge, 0) == 0) {
        std::string rest = rawBase.substr(ge.size());
        size_t a = rest.find('/');
        size_t b = a == std::string::npos ? std::string::npos : rest.find('/', a + 1);
        size_t c = b == std::string::npos ? std::string::npos : rest.find('/', b + 1);
        size_t d = c == std::string::npos ? std::string::npos : rest.find('/', c + 1);
        if (a == std::string::npos || b == std::string::npos ||
            c == std::string::npos || d == std::string::npos)
            return "";
        if (rest.substr(b + 1, c - b - 1) != "raw") return "";
        return "https://gitee.com/api/v5/repos/" + rest.substr(0, b) +
               "/contents/langs?ref=" + rest.substr(c + 1, d - c - 1);
    }
    return "";
}

// 当前源的在线语言列表 API（两端均返回 [{"type":"file","name":"xx.cjl"}] 数组；custom 推不出返回空）
inline std::string langSourceApiUrl() {
    std::string s = langSource();
    if (s == "gitee")
        return "https://gitee.com/api/v5/repos/" + GITEE_OWNER + "/" + REPO_NAME +
               "/contents/langs?ref=" + LANGS_BRANCH;
    if (s == "custom") {
        std::string u = langSourceCustomRaw();
        if (u.empty()) return "";
        return deriveApiUrl(u);
    }
    return "https://api.github.com/repos/" + REPO_OWNER + "/" + REPO_NAME +
           "/contents/langs?ref=" + LANGS_BRANCH;
}

// 通过 HTTP GET 获取内容
#ifdef _WIN32
inline std::string httpGet(const std::string& url) {
    // 手动解析 URL: https://host/path
    std::string host, path;
    int port = 443;
    
    // 去掉 https:// 前缀
    std::string remaining = url;
    if (remaining.find("https://") == 0) {
        remaining = remaining.substr(8);
    } else if (remaining.find("http://") == 0) {
        remaining = remaining.substr(7);
        port = 80;
    }
    
    // 分离 host 和 path
    size_t slashPos = remaining.find('/');
    if (slashPos != std::string::npos) {
        host = remaining.substr(0, slashPos);
        path = remaining.substr(slashPos);
    } else {
        host = remaining;
        path = "/";
    }
    
    // 检查是否有端口号 (端口非法时保持默认端口, 避免 std::stoi 抛出导致崩溃)
    size_t colonPos = host.find(':');
    if (colonPos != std::string::npos) {
        try {
            port = std::stoi(host.substr(colonPos + 1));
            host = host.substr(0, colonPos);
        } catch (...) {
        }
    }
    
    // 转换为宽字符
    std::wstring wHost(host.begin(), host.end());
    std::wstring wPath(path.begin(), path.end());
    
    // 建立连接
    HINTERNET hSession = WinHttpOpen(L"CliJudge/1.0", 
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";
    // 源检测等交互路径不能挂在默认 60s/阶段超时上（死主机最长 3~4 分钟）
    WinHttpSetTimeouts(hSession, 15000, 15000, 15000, 15000);
    
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(),
                                         (INTERNET_PORT)port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    DWORD flags = WINHTTP_FLAG_SECURE;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(),
                                             NULL, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    // 忽略 SSL 证书验证
    DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                          SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                          SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                          SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
    
    // 发送请求
    BOOL result = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    result = WinHttpReceiveResponse(hRequest, NULL);
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    // 校验 HTTP 状态码: 否则 404 的响应体会被当成语言包/列表内容保存 (Linux 侧 curl -f 同语义)
    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(hRequest,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                             WINHTTP_NO_HEADER_INDEX) ||
        statusCode < 200 || statusCode >= 300) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    // 读取响应
    std::string response;
    DWORD bytesRead = 0;
    char buffer[4096];
    
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        response.append(buffer, bytesRead);
        bytesRead = 0;
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return response;
}
#else
// Linux: 通过 curl 获取（curl 在各发行版上均为常见基础组件）
inline std::string httpGet(const std::string& url) {
    // URL 将拼入 shell 命令，先做白名单校验（防止注入）
    for (char c : url) {
        bool ok = (unsigned char)c < 0x80 &&
                  (isalnum((unsigned char)c) || std::strchr(":/._-~?&=%+@,", c) != nullptr);
        if (!ok) return "";
    }
    std::string cmd = "curl -fsSL --max-time 30 '" + url + "'";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    int rc = pclose(p);
    if (rc != 0) return "";
    return out;
}
#endif

// 获取在线语言列表
inline json getOnlineLangs() {
    // 按当前语言包源（github/gitee/custom）查询 langs 目录下的文件列表
    std::string url = langSourceApiUrl();
    if (url.empty()) return json::array();
    
    std::string response = httpGet(url);
    if (response.empty()) {
        return json::array();
    }
    
    try {
        json data = json::parse(response);
        if (!data.is_array()) return json::array();
        
        json langs = json::array();
        for (const auto& item : data) {
            if (item.value("type", "") == "file") {
                std::string name = item.value("name", "");
                if (name.size() > 4 && name.substr(name.size() - 4) == ".cjl") {
                    langs.push_back(name.substr(0, name.size() - 4));
                }
            }
        }
        return langs;
    } catch (...) {
        return json::array();
    }
}

// 下载语言文件（en 下载失败时回退到内置语言包，保证离线可用）
inline bool downloadLang(const std::string& langName, bool* usedBuiltin = nullptr) {
    if (usedBuiltin) *usedBuiltin = false;
    if (!validLangName(langName)) {
        std::cerr << trf("err.invalid_lang_name", "Invalid language name: {0}", {langName}) << std::endl;
        return false;
    }
    std::string url = langSourceRawBase() + langName + ".cjl";
    
    std::string content = httpGet(url);
    if (content.empty()) {
        if (langName == "en") {
            content = builtinEnJson();
            if (usedBuiltin) *usedBuiltin = true;
        } else {
            std::cerr << trf("err.download_lang_file_failed", "Failed to download language file: {0}", {langName}) << std::endl;
            return false;
        }
    }
    
    std::string path = platform::pathJoin(getLangsDir(), langName + ".cjl");
    // 落盘前校验内容是合法 JSON 对象: 拦截错误页/HTML/非语言包内容
    // (Windows 404 之前不查状态码; 即使 200 也可能是代理返回的 HTML)
    try {
        json probe = json::parse(content);
        if (!probe.is_object()) throw std::runtime_error("not an object");
    } catch (...) {
        std::cerr << trf("err.download_lang_file_invalid",
                         "Failed to download language file: {0} (response is not a valid language pack)",
                         {langName}) << std::endl;
        return false;
    }
    if (!writeFile(path, content)) {
        std::cerr << trf("err.save_lang_failed", "Failed to save language file: {0}", {langName}) << std::endl;
        return false;
    }
    
    return true;
}

// 加载语言文件
inline json loadLang(const std::string& langName) {
    if (!validLangName(langName)) return nullptr;
    std::string path = platform::pathJoin(getLangsDir(), langName + ".cjl");
    std::string content = readFile(path);
    if (content.empty()) {
        return nullptr;
    }
    try {
        return json::parse(content);
    } catch (...) {
        return nullptr;
    }
}

// 获取本地语言列表
inline json getLocalLangs() {
    std::string langsDir = getLangsDir();
    json langs = json::array();
    
    if (!fs::exists(langsDir)) {
        return langs;
    }
    
    for (const auto& entry : fs::directory_iterator(langsDir)) {
        if (entry.is_regular_file()) {
            std::string name = entry.path().filename().string();
            if (name.size() > 4 && name.substr(name.size() - 4) == ".cjl") {
                std::string langName = name.substr(0, name.size() - 4);
                json langInfo = loadLang(langName);
                json item;
                item["name"] = langName;
                if (!langInfo.is_null() && langInfo.contains("meta")) {
                    item["display"] = langInfo["meta"].value("display", langName);
                    item["version"] = langInfo["meta"].value("version", "1.0.0");
                } else {
                    item["display"] = langName;
                    item["version"] = "unknown";
                }
                langs.push_back(item);
            }
        }
    }
    
    return langs;
}

// ── 本地化字符串获取 ──────────────────────────────────────────

// 全局语言数据缓存
inline json& getLangData() {
    static json langData = nullptr;
    static bool loaded = false;
    
    if (!loaded) {
        loaded = true;
        std::string current = getCurrentLang();
        if (!current.empty()) {
            langData = loadLang(current);
        }
    }
    
    return langData;
}

// 按点分隔键路径查找节点，未找到返回 nullptr
inline const json* lookupPath(const json& node, const std::string& key) {
    if (!node.is_object()) return nullptr;
    const json* p = &node;
    std::istringstream ss(key);
    std::string segment;
    while (std::getline(ss, segment, '.')) {
        if (!p->is_object() || !p->contains(segment)) return nullptr;
        p = &(*p)[segment];
    }
    return p;
}

// 获取本地化字符串
// 键路径从根查找；若根下不存在则回退到 "strings" 包装层（.cjl 的实际结构）
inline std::string tr(const std::string& key, const std::string& fallback = "") {
    json& langData = getLangData();
    if (langData.is_null()) {
        return fallback.empty() ? key : fallback;
    }
    
    const json* node = lookupPath(langData, key);
    if (!node && langData.contains("strings")) {
        node = lookupPath(langData["strings"], key);
    }
    if (node && node->is_string()) {
        return node->get<std::string>();
    }
    return fallback.empty() ? key : fallback;
}

// 带占位符翻译: 译文/回退串里的 {0} {1} ... 按序替换为实参。
// 用于含编号/路径的输出消息, 避免一条消息拆成前后两个 key。
inline std::string trf(const std::string& key, const std::string& fallback,
                       const std::vector<std::string>& args) {
    std::string s = tr(key, fallback);
    for (size_t i = 0; i < args.size(); i++) {
        std::string ph = "{" + std::to_string(i) + "}";
        size_t p = s.find(ph);
        if (p == std::string::npos) continue;
        s.replace(p, ph.size(), args[i]);
    }
    return s;
}

// ── 子命令实现 ────────────────────────────────────────────────

// list - 显示本地/在线语言
inline int cmdList(bool online = false) {
    if (online) {
        std::cout << tr("displaylang.online_header", "Online languages:") << std::endl;
        json langs = getOnlineLangs();
        if (langs.empty()) {
            std::cout << tr("displaylang.online_none", "  (none or network error)") << std::endl;
            return 0;
        }
        std::string current = getCurrentLang();
        for (const auto& lang : langs) {
            std::string name = lang.get<std::string>();
            std::string marker = (name == current) ? " *" : "";
            std::cout << "  " << name << marker << std::endl;
        }
        std::cout << std::endl;
        std::cout << tr("displaylang.active_marker", "* = currently active") << std::endl;
    } else {
        std::cout << tr("displaylang.local_header", "Local languages:") << std::endl;
        json langs = getLocalLangs();
        if (langs.empty()) {
            std::cout << tr("displaylang.local_none", "  (none)") << std::endl;
            std::cout << std::endl;
            std::cout << tr("displaylang.hint_list", "Use 'clijudge displaylang list --online' to see available languages.") << std::endl;
            std::cout << tr("displaylang.hint_switch", "Use 'clijudge displaylang switch [langname]' to download and activate.") << std::endl;
            return 0;
        }
        std::string current = getCurrentLang();
        for (const auto& lang : langs) {
            std::string name = lang.value("name", "");
            std::string display = lang.value("display", name);
            std::string version = lang.value("version", "");
            std::string marker = (name == current) ? " *" : "";
            std::cout << "  " << name << " (" << display << ") v" << version << marker << std::endl;
        }
        std::cout << std::endl;
        std::cout << tr("displaylang.active_marker", "* = currently active") << std::endl;
    }
    return 0;
}

// switch - 切换语言
inline int cmdSwitch(const std::string& langName) {
    if (langName.empty()) {
        std::cerr << tr("usage.displaylang_switch", "Usage: clijudge displaylang switch [langname]") << std::endl;
        return 1;
    }
    
    // 检查本地是否已有
    std::string path = platform::pathJoin(getLangsDir(), langName + ".cjl");
    if (!fs::exists(path)) {
        std::cout << trf("displaylang.not_found_downloading", "Language '{0}' not found locally. Downloading...", {langName}) << std::endl;
        bool usedBuiltin = false;
        if (!downloadLang(langName, &usedBuiltin)) {
            std::cerr << trf("err.download_lang_failed", "Failed to download language: {0}", {langName}) << std::endl;
            return 1;
        }
        if (usedBuiltin) {
            std::cout << tr("displaylang.using_builtin", "Using built-in language pack.") << std::endl;
        } else {
            std::cout << tr("success.downloaded", "Downloaded successfully") << "." << std::endl;
        }
    }
    
    // 验证文件有效
    json langData = loadLang(langName);
    if (langData.is_null()) {
        std::cerr << trf("err.invalid_lang_file", "Invalid language file: {0}", {langName}) << std::endl;
        return 1;
    }
    
    // 保存配置
    json config = loadConfig();
    config["current_lang"] = langName;
    if (saveConfig(config)) {
        std::cout << tr("success.switched", "Switched to language") << ": " << langName << std::endl;
    } else {
        std::cerr << tr("err.save_config_failed", "Failed to save configuration.") << std::endl;
        return 1;
    }
    
    return 0;
}

// delete - 删除本地语言
inline int cmdDelete(const std::string& langName) {
    if (langName.empty()) {
        std::cerr << tr("usage.displaylang_delete", "Usage: clijudge displaylang delete [langname]") << std::endl;
        return 1;
    }
    if (!validLangName(langName)) {
        std::cerr << trf("err.invalid_lang_name", "Invalid language name: {0}", {langName}) << std::endl;
        return 1;
    }
    // en 为内置语言（二进制内嵌 + 离线回退），本地文件只是缓存副本，
    // 删除只会清空 current_lang 逼用户重新 switch，没有意义
    if (langName == "en") {
        std::cerr << tr("displaylang.builtin_en_delete",
                        "Built-in language 'en' cannot be deleted. "
                        "Use 'clijudge displaylang switch [langname]' to change language, "
                        "or 'clijudge displaylang pull en' to refresh the local copy.") << std::endl;
        return 1;
    }

    std::string path = platform::pathJoin(getLangsDir(), langName + ".cjl");
    if (!fs::exists(path)) {
        std::cerr << trf("err.lang_file_not_found", "Language file not found: {0}", {langName}) << std::endl;
        return 1;
    }
    
    // 如果正在使用该语言，先取消
    std::string current = getCurrentLang();
    if (current == langName) {
        json config = loadConfig();
        config["current_lang"] = "";
        saveConfig(config);
        std::cout << tr("displaylang.deactivated", "Deactivated current language.") << std::endl;
    }
    
    if (fs::remove(path)) {
        std::cout << trf("displaylang.deleted", "Deleted language: {0}", {langName}) << std::endl;
    } else {
        std::cerr << tr("err.delete_lang_failed", "Failed to delete language file.") << std::endl;
        return 1;
    }
    
    return 0;
}

// pull - 拉取语言文件（不切换）
inline int cmdPull(const std::string& langName) {
    if (langName.empty()) {
        std::cerr << tr("usage.displaylang_pull", "Usage: clijudge displaylang pull [langname]") << std::endl;
        return 1;
    }
    
    std::cout << trf("displaylang.downloading", "Downloading language: {0}...", {langName}) << std::endl;
    if (downloadLang(langName)) {
        std::cout << trf("displaylang.updated", "Updated language: {0}", {langName}) << std::endl;
    } else {
        std::cerr << trf("err.download_lang_failed", "Failed to download language: {0}", {langName}) << std::endl;
        return 1;
    }
    
    return 0;
}

// 自定义源在线检测: GET <base>en.cjl 须为含 strings 的 JSON 对象
// 返回空串 = 通过; 否则为已本地化的错误信息
inline std::string checkSourceUrl(std::string baseUrl) {
    if (baseUrl.rfind("http://", 0) != 0 && baseUrl.rfind("https://", 0) != 0)
        return trf("displaylang.source_invalid_url",
                   "Invalid source URL: {0} (must start with http:// or https://)", {baseUrl});
    if (baseUrl.back() != '/') baseUrl += '/';
    std::string content = httpGet(baseUrl + "en.cjl");
    if (content.empty())
        return trf("displaylang.source_unreachable",
                   "Source check failed: cannot download en.cjl from {0}", {baseUrl});
    try {
        json probe = json::parse(content);
        if (!probe.is_object() || !probe.contains("strings") || !probe["strings"].is_object())
            throw std::runtime_error("not a language pack");
    } catch (...) {
        return trf("displaylang.source_not_pack",
                   "Source check failed: {0} does not serve a valid language pack", {baseUrl});
    }
    return "";
}

// source - 查询/切换语言包源（github / gitee / custom，custom 设置时在线检测）
inline int cmdSource(const std::string& name, const std::string& url) {
    if (name.empty()) {
        std::cout << trf("displaylang.source_current", "Language pack source: {0}", {langSource()}) << std::endl;
        if (langSource() == "custom") {
            std::string u = langSourceCustomRaw();
            if (!u.empty())
                std::cout << trf("displaylang.source_current_url", "Language pack source URL: {0}", {u}) << std::endl;
        }
        return 0;
    }
    if (name != "github" && name != "gitee" && name != "custom") {
        std::cerr << trf("displaylang.source_invalid", "Invalid source: {0} (expected github, gitee or custom)", {name}) << std::endl;
        return 1;
    }
    json config = loadConfig();
    std::string customBase;
    if (name == "custom") {
        customBase = url.empty() ? config.value("lang_source_url", "") : url;
        if (customBase.empty()) {
            std::cerr << tr("usage.displaylang_source_custom", "Usage: clijudge displaylang source custom <base-url>") << std::endl;
            return 1;
        }
        std::string checkErr = checkSourceUrl(customBase);
        if (!checkErr.empty()) {
            std::cerr << checkErr << std::endl;
            return 1;
        }
        if (customBase.back() != '/') customBase += '/';
        config["lang_source_url"] = customBase;
    }
    config["lang_source"] = name;
    if (!saveConfig(config)) {
        std::cerr << tr("displaylang.source_save_failed", "Failed to save language source setting.") << std::endl;
        return 1;
    }
    std::string shown = name == "custom" ? "custom (" + customBase + ")" : name;
    std::cout << trf("displaylang.source_set", "Language pack source set to: {0}", {shown}) << std::endl;
    return 0;
}

// 检查是否已配置语言
inline bool isLangConfigured() {
    ensureDefaultLang(); // 首次运行自动配置内置英文包（离线可用）
    std::string current = getCurrentLang();
    if (current.empty()) return false;
    json langData = loadLang(current);
    return !langData.is_null();
}

// 显示未配置语言的错误信息
inline void showNoLangError() {
    std::cerr << tr("error.no_lang", "Error: No display language configured.") << std::endl;
    std::cerr << std::endl;
    std::cerr << tr("error.no_lang_hint", "Please run: clijudge displaylang switch [langname]") << std::endl;
    std::cerr << tr("error.no_lang_list", "Use 'clijudge displaylang list --online' to see available languages.") << std::endl;
    std::cerr << tr("displaylang.offline_fallback", "Offline fallback: clijudge displaylang switch en") << std::endl;
}

} // namespace lang
} // namespace clijudge

#endif // CLIJUDGE_LANG_H