// tests/test_judge.cpp
// 判题核心纯函数单测
//
// 覆盖范围: 只测与编译器、沙箱、文件系统无关的纯函数
//   - 文本归一化 / 拆分 (normalizeEol, trimLineEnd, splitLines, splitLinesTrim)
//   - 分词 (tokenizeWithLines, parseLongDouble)
//   - 比较器 (compareStrict, compareLineByLine, compareIgnoreSpace, compareFloat)
//   - 比较模式分发 (compareOutputs) 与 SPJ 模式判定 (isSpecialJudgeMode)
//   - testlib SPJ 输出解析 (parseTestlibVerdict)
//   - 判定结果文案 (statusToString, statusToAbbr)
//   - 超时宽限 (computeKillLimitMs)
//
// 构建: g++ -std=c++17 -O2 -Wall -I include -o judge_tests tests/test_judge.cpp
// 退出码 0 表示全部通过

#include "judge.h"

#include <iostream>
#include <sstream>

namespace {

namespace judge = clijudge::judge;

int g_checks = 0;
int g_failed = 0;
std::string g_group;

void beginGroup(const char* name) {
    g_group = name;
}

void fail(const std::string& name, const std::string& detail) {
    g_failed++;
    std::cerr << "  [FAIL] " << g_group << " / " << name;
    if (!detail.empty()) std::cerr << "  (" << detail << ")";
    std::cerr << "\n";
}

void check(bool ok, const std::string& name, const std::string& detail = "") {
    g_checks++;
    if (!ok) fail(name, detail);
}

template <typename A, typename B>
void checkEq(const std::string& name, const A& actual, const B& expected) {
    g_checks++;
    if (!(actual == expected)) {
        std::ostringstream os;
        os << "actual=" << actual << " expected=" << expected;
        fail(name, os.str());
    }
}

std::string joined(const std::vector<std::string>& v) {
    std::string r;
    for (size_t i = 0; i < v.size(); i++) {
        if (i) r += "|";
        r += "[" + v[i] + "]";
    }
    return r;
}

// ── 文本归一化 ───────────────────────────────────────────────
void testNormalize() {
    beginGroup("normalize");

    checkEq("CRLF 归一化为 LF", judge::normalizeEol("a\r\nb\r\n"), std::string("a\nb\n"));
    checkEq("孤立 CR 保留", judge::normalizeEol("a\rb"), std::string("a\rb"));
    checkEq("无换行原样返回", judge::normalizeEol("abc"), std::string("abc"));
    checkEq("空串", judge::normalizeEol(""), std::string(""));

    checkEq("去掉行末空白", judge::trimLineEnd("abc \t\r\n"), std::string("abc"));
    checkEq("全空白行清空", judge::trimLineEnd("   \t"), std::string(""));
    checkEq("无空白不动", judge::trimLineEnd("abc"), std::string("abc"));
    checkEq("空串", judge::trimLineEnd(""), std::string(""));
}

void testSplit() {
    beginGroup("split");

    checkEq("尾随换行不产生空行", joined(judge::splitLines("a\nb\n")), std::string("[a]|[b]"));
    checkEq("无尾随换行", joined(judge::splitLines("a\nb")), std::string("[a]|[b]"));
    checkEq("末尾连续换行只去掉一个",
            joined(judge::splitLines("a\n\n")), std::string("[a]|[]"));
    checkEq("空串视为一个空行", joined(judge::splitLines("")), std::string("[]"));
    checkEq("CR 保留在行尾", joined(judge::splitLines("a\r\nb")), std::string("[a\r]|[b]"));

    checkEq("逐行去行末空白",
            joined(judge::splitLinesTrim("a  \nb\t\n")), std::string("[a]|[b]"));
    checkEq("CRLF 下也去掉 CR",
            joined(judge::splitLinesTrim("a\r\nb\r\n")), std::string("[a]|[b]"));
}

void testTokenize() {
    beginGroup("tokenize");

    auto t = judge::tokenizeWithLines("1 2\n3");
    checkEq("token 数量", (int)t.size(), 3);
    checkEq("token0 文本", t[0].text, std::string("1"));
    checkEq("token1 行号", t[1].line, 1);
    checkEq("token2 文本", t[2].text, std::string("3"));
    checkEq("token2 行号", t[2].line, 2);

    checkEq("空串无 token", (int)judge::tokenizeWithLines("").size(), 0);
    checkEq("纯空白无 token", (int)judge::tokenizeWithLines(" \t\r\n ").size(), 0);

    auto m = judge::tokenizeWithLines("a\n\nb");
    checkEq("空行使行号跳过", m.size() == 2 ? m[1].line : -1, 3);
}

void testParseLongDouble() {
    beginGroup("parseLongDouble");

    long double v = 0;
    check(judge::parseLongDouble("42", v) && v == 42.0L, "整数");
    check(judge::parseLongDouble("3.25", v) && v == 3.25L, "小数");
    check(judge::parseLongDouble("1e3", v) && v == 1000.0L, "科学计数");
    check(judge::parseLongDouble("-7", v) && v == -7.0L, "负数");
    check(judge::parseLongDouble("inf", v) && std::isinf(v), "inf 可解析");
    check(judge::parseLongDouble("nan", v) && std::isnan(v), "nan 可解析");
    check(!judge::parseLongDouble("abc", v), "非数字拒绝");
    check(!judge::parseLongDouble("1.2x", v), "残留字符拒绝");
    check(!judge::parseLongDouble("", v), "空串拒绝");
    check(!judge::parseLongDouble("-", v), "纯符号拒绝");
}

// ── 比较器 ───────────────────────────────────────────────────
using judge::CompareResult;
using judge::JudgeStatus;

void checkResult(const std::string& name, const CompareResult& r,
                 JudgeStatus status, int score) {
    check(r.status == status, name + " 状态",
          "actual=" + judge::statusToAbbr(r.status) + " expected=" + judge::statusToAbbr(status));
    checkEq(name + " 分数", r.score, score);
}

void testCompareStrict() {
    beginGroup("compareStrict");

    checkResult("完全相同", judge::compareStrict("1 2\n", "1 2\n", 10),
                JudgeStatus::ACCEPTED, 10);
    checkResult("CRLF 等价", judge::compareStrict("1\n2\n", "1\r\n2\r\n", 10),
                JudgeStatus::ACCEPTED, 10);
    checkResult("行末空格不同判错", judge::compareStrict("1\n", "1 \n", 10),
                JudgeStatus::WRONG_ANSWER, 0);
    checkResult("缺少尾随换行", judge::compareStrict("1\n", "1", 10),
                JudgeStatus::WRONG_ANSWER, 0);
    checkResult("内容不同", judge::compareStrict("a", "b", 7), JudgeStatus::WRONG_ANSWER, 0);
    checkResult("空输出对空答案", judge::compareStrict("", "", 5), JudgeStatus::ACCEPTED, 5);
}

void testCompareLineByLine() {
    beginGroup("compareLineByLine");

    checkResult("行末空白忽略", judge::compareLineByLine("1\n2\n", "1  \n2\t\n", 10),
                JudgeStatus::ACCEPTED, 10);
    checkResult("缺尾随换行可接受", judge::compareLineByLine("1\n2\n", "1\n2", 10),
                JudgeStatus::ACCEPTED, 10);
    checkResult("选手内容少", judge::compareLineByLine("1\n2\n", "1\n", 10),
                JudgeStatus::WRONG_ANSWER, 0);
    checkResult("选手内容多", judge::compareLineByLine("1\n", "1\n2\n", 10),
                JudgeStatus::OUTPUT_LIMIT_EXCEEDED, 0);

    CompareResult r = judge::compareLineByLine("1\n2\n", "1\n9\n", 10);
    checkResult("第二行不同", r, JudgeStatus::WRONG_ANSWER, 0);
    check(r.message.find("line 2") != std::string::npos, "错误信息含行号", r.message);
}

void testCompareIgnoreSpace() {
    beginGroup("compareIgnoreSpace");

    checkResult("token 与行结构均相同", judge::compareIgnoreSpace("1 2\n3\n", "1  2\n  3\n", 10),
                JudgeStatus::ACCEPTED, 10);
    checkResult("行结构不同判 PE", judge::compareIgnoreSpace("1 2\n3\n", "1\n2 3\n", 10),
                JudgeStatus::PRESENTATION_ERROR, 0);
    checkResult("同行数不同分组也判 PE", judge::compareIgnoreSpace("1\n2 3", "1 2\n3", 10),
                JudgeStatus::PRESENTATION_ERROR, 0);
    checkResult("token 不同", judge::compareIgnoreSpace("1 2 3", "1 3 3", 10),
                JudgeStatus::WRONG_ANSWER, 0);
    checkResult("选手少 token", judge::compareIgnoreSpace("1 2 3", "1 2", 10),
                JudgeStatus::WRONG_ANSWER, 0);
    checkResult("选手多 token", judge::compareIgnoreSpace("1 2", "1 2 3", 10),
                JudgeStatus::OUTPUT_LIMIT_EXCEEDED, 0);
}

void testCompareFloat() {
    beginGroup("compareFloat");

    checkResult("绝对误差内",
                judge::compareFloat("1.0000001", "1.0", 10, "float_abs", 1e-6, 1e-6),
                JudgeStatus::ACCEPTED, 10);
    checkResult("绝对误差外",
                judge::compareFloat("1.1", "1.0", 10, "float_abs", 1e-6, 1e-6),
                JudgeStatus::WRONG_ANSWER, 0);
    checkResult("默认绝对容差 1e-6",
                judge::compareFloat("1.0000001", "1.0", 10, "float_abs", 0.0, 0.0),
                JudgeStatus::ACCEPTED, 10);

    checkResult("相对误差内",
                judge::compareFloat("1000000000", "1000000100", 10, "float_rel", 1e-6, 1e-6),
                JudgeStatus::ACCEPTED, 10);
    checkResult("相对误差外",
                judge::compareFloat("1000", "1001", 10, "float_rel", 1e-6, 1e-6),
                JudgeStatus::WRONG_ANSWER, 0);

    checkResult("综合误差按相对放宽",
                judge::compareFloat("1000000", "1000000.5", 10, "float_all", 1e-6, 1e-6),
                JudgeStatus::ACCEPTED, 10);
    checkResult("综合误差超出两者",
                judge::compareFloat("10", "10.001", 10, "float_all", 1e-6, 1e-6),
                JudgeStatus::WRONG_ANSWER, 0);

    checkResult("NaN 同型可接受",
                judge::compareFloat("nan", "nan", 10, "float_abs", 1e-6, 1e-6),
                JudgeStatus::ACCEPTED, 10);
    checkResult("NaN 与其他判错",
                judge::compareFloat("nan", "1", 10, "float_abs", 1e-6, 1e-6),
                JudgeStatus::WRONG_ANSWER, 0);
    checkResult("正负无穷不同型",
                judge::compareFloat("inf", "-inf", 10, "float_abs", 1e-6, 1e-6),
                JudgeStatus::WRONG_ANSWER, 0);
    checkResult("同为无穷可接受",
                judge::compareFloat("inf", "inf", 10, "float_abs", 1e-6, 1e-6),
                JudgeStatus::ACCEPTED, 10);

    checkResult("非数字 token 全等",
                judge::compareFloat("abc", "abc", 10, "float_abs", 1e-6, 1e-6),
                JudgeStatus::ACCEPTED, 10);
    checkResult("非数字 token 不同",
                judge::compareFloat("abc", "abd", 10, "float_abs", 1e-6, 1e-6),
                JudgeStatus::WRONG_ANSWER, 0);

    checkResult("实数少 token",
                judge::compareFloat("1 2", "1", 10, "float_abs", 1e-6, 1e-6),
                JudgeStatus::WRONG_ANSWER, 0);
    checkResult("实数多 token",
                judge::compareFloat("1", "1 2", 10, "float_abs", 1e-6, 1e-6),
                JudgeStatus::OUTPUT_LIMIT_EXCEEDED, 0);
}

void testCompareOutputs() {
    beginGroup("compareOutputs");

    checkResult("text_strict 分发",
                judge::compareOutputs("text_strict", "1\n", "1 \n", 10, 1e-6, 1e-6),
                JudgeStatus::WRONG_ANSWER, 0);
    checkResult("text_line 分发",
                judge::compareOutputs("text_line", "1\n", "1 \n", 10, 1e-6, 1e-6),
                JudgeStatus::ACCEPTED, 10);
    checkResult("text_no_space 分发",
                judge::compareOutputs("text_no_space", "1 2\n3", "1\n2 3", 10, 1e-6, 1e-6),
                JudgeStatus::PRESENTATION_ERROR, 0);
    checkResult("float_all 分发",
                judge::compareOutputs("float_all", "1.0", "1.1", 10, 1e-6, 1e-6),
                JudgeStatus::WRONG_ANSWER, 0);
    checkResult("未知模式退回严格比较",
                judge::compareOutputs("", "1", "1", 10, 1e-6, 1e-6),
                JudgeStatus::ACCEPTED, 10);

    check(judge::isSpecialJudgeMode("spj"), "spj 模式判定");
    check(judge::isSpecialJudgeMode("spj_lemon"), "spj_lemon 模式判定");
    check(judge::isSpecialJudgeMode("spj_testlib"), "spj_testlib 模式判定");
    check(!judge::isSpecialJudgeMode("text_line"), "普通模式不误判");
    check(!judge::isSpecialJudgeMode(""), "空模式不误判");
}

void testTestlibVerdict() {
    beginGroup("testlibVerdict");

    auto ok = judge::parseTestlibVerdict("ok\n", 100);
    check(ok.matched, "ok 匹配");
    checkEq("ok 状态", judge::statusToAbbr(ok.status), std::string("AC"));
    checkEq("ok 满分", ok.score, 100);

    auto okMsg = judge::parseTestlibVerdict("ok single file", 30);
    checkEq("ok 带说明满分", okMsg.score, 30);
    checkEq("ok 保留说明", okMsg.message, std::string("ok single file"));

    auto bad = judge::parseTestlibVerdict("FAIL n = 3\n", 100);
    check(bad.matched, "FAIL 匹配");
    checkEq("FAIL 状态", judge::statusToAbbr(bad.status), std::string("IV"));

    auto pc = judge::parseTestlibVerdict("partially correct (50)\n", 100);
    checkEq("部分正确状态", judge::statusToAbbr(pc.status), std::string("PC"));
    checkEq("部分正确分数", pc.score, 50);

    auto pcScaled = judge::parseTestlibVerdict("partially correct (50)", 30);
    checkEq("部分正确按满分折算", pcScaled.score, 15);

    auto pcFull = judge::parseTestlibVerdict("partially correct (100)", 100);
    checkEq("部分正确满 100 视为 AC", judge::statusToAbbr(pcFull.status), std::string("AC"));
    checkEq("部分正确满 100 满分", pcFull.score, 100);

    auto pcZero = judge::parseTestlibVerdict("partially correct (0)", 100);
    checkEq("部分正确 0 视为 WA", judge::statusToAbbr(pcZero.status), std::string("WA"));
    checkEq("部分正确 0 零分", pcZero.score, 0);

    auto pts = judge::parseTestlibVerdict("points 1.0", 100);
    checkEq("points 1.0 满分", pts.score, 100);
    checkEq("points 1.0 状态", judge::statusToAbbr(pts.status), std::string("AC"));

    auto ptsHalf = judge::parseTestlibVerdict("points 0.5", 100);
    checkEq("points 0.5 半分", ptsHalf.score, 50);
    checkEq("points 0.5 状态", judge::statusToAbbr(ptsHalf.status), std::string("PC"));

    auto wrong = judge::parseTestlibVerdict("wrong answer 1st numbers differ", 100);
    check(wrong.matched, "wrong 匹配");
    checkEq("wrong 状态", judge::statusToAbbr(wrong.status), std::string("WA"));

    auto unknown = judge::parseTestlibVerdict("something unexpected", 100);
    check(!unknown.matched, "未知输出不匹配");
    checkEq("未知输出状态", judge::statusToAbbr(unknown.status), std::string("WA"));
    checkEq("未知输出零分", unknown.score, 0);
}

void testStatusText() {
    beginGroup("statusText");

    struct Row { JudgeStatus s; const char* text; const char* abbr; };
    const Row rows[] = {
        {JudgeStatus::ACCEPTED, "Accepted", "AC"},
        {JudgeStatus::WRONG_ANSWER, "Wrong Answer", "WA"},
        {JudgeStatus::TIME_LIMIT_EXCEEDED, "Time Limit Exceeded", "TLE"},
        {JudgeStatus::MEMORY_LIMIT_EXCEEDED, "Memory Limit Exceeded", "MLE"},
        {JudgeStatus::RUNTIME_ERROR, "Runtime Error", "RE"},
        {JudgeStatus::COMPILATION_ERROR, "Compilation Error", "CE"},
        {JudgeStatus::SYSTEM_ERROR, "System Error", "SE"},
        {JudgeStatus::SKIPPED, "Skipped", "SK"},
        {JudgeStatus::PRESENTATION_ERROR, "Presentation Error", "PE"},
        {JudgeStatus::OUTPUT_LIMIT_EXCEEDED, "Output Limit Exceeded", "OLE"},
        {JudgeStatus::PARTIALLY_CORRECT, "Partially Correct", "PC"},
        {JudgeStatus::INVALID_SPJ, "Invalid Special Judge", "IV"},
        {JudgeStatus::SPECIAL_JUDGE_TLE, "Special Judge Time Limit Exceeded", "ST"},
        {JudgeStatus::SPECIAL_JUDGE_RE, "Special Judge Runtime Error", "SR"},
        {JudgeStatus::INTERACTOR_ERROR, "Interactor Error", "IE"},
    };
    for (const Row& r : rows) {
        checkEq(std::string("文案 ") + r.abbr, judge::statusToString(r.s), std::string(r.text));
        checkEq(std::string("缩写 ") + r.abbr, judge::statusToAbbr(r.s), std::string(r.abbr));
    }

    checkEq("未知状态文案", judge::statusToString(static_cast<JudgeStatus>(9999)),
            std::string("Unknown"));
    checkEq("未知状态缩写", judge::statusToAbbr(static_cast<JudgeStatus>(9999)),
            std::string("??"));
}

void testKillLimit() {
    beginGroup("killLimit");

    checkEq("1s/ratio1", judge::computeKillLimitMs(1000, 1.0), 3000);
    checkEq("1s/ratio0.5", judge::computeKillLimitMs(1000, 0.5), 2000);
    checkEq("1.5s 向上取整到 2s", judge::computeKillLimitMs(1500, 1.0), 5000);
    checkEq("100ms 至少 2s 宽限", judge::computeKillLimitMs(100, 1.0), 3000);
    checkEq("0ms", judge::computeKillLimitMs(0, 1.0), 2000);
    checkEq("长时限按 2 倍给宽限", judge::computeKillLimitMs(5000, 0.5), 10000);
}

void testTrimTail() {
    beginGroup("trimTail");

    checkEq("短串不变", judge::trimTail("abc", 10), std::string("abc"));
    std::string s = judge::trimTail("0123456789", 4);
    check(s.find("... (truncated)\n") == 0, "截断含提示", s);
    check(s.size() >= 4 && s.substr(s.size() - 4) == "6789", "截断保留尾部", s);
}

}  // namespace

int main() {
    testNormalize();
    testSplit();
    testTokenize();
    testParseLongDouble();
    testCompareStrict();
    testCompareLineByLine();
    testCompareIgnoreSpace();
    testCompareFloat();
    testCompareOutputs();
    testTestlibVerdict();
    testStatusText();
    testKillLimit();
    testTrimTail();

    std::cout << "\njudge unit tests: " << (g_checks - g_failed) << "/" << g_checks
              << " passed, " << g_failed << " failed\n";
    if (g_failed) {
        std::cerr << "FAILED\n";
        return 1;
    }
    std::cout << "OK\n";
    return 0;
}
