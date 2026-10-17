# CliJudge 命令行使用文档

所有功能通过 `clijudge <命令> [子命令] [参数...]` 调用（Windows 下可执行文件为 `clijudge.exe`）。列表类 / 详情类命令输出缩进 JSON（两空格 `dump(2)`），可直接被脚本解析。

## 通用说明

### 帮助

```
clijudge help                    # 总帮助
clijudge <命令> help             # 分模块帮助（article/contest/ide/problem/submit/displaylang）
```

语言检查按**顶层命令**判定：仅顶层 `help` 与 `displaylang` 不要求已配置显示语言，`problem help` 等子命令帮助同样受此限制。

### 数据目录

1. 环境变量 `CLIJUDGE_DATA_DIR` 指定的目录（最高优先级）
2. 否则为可执行文件同级的 `data/`

目录结构：

```
data/
├── config.json          # current_lang + "judge" 评测设置
├── langs/*.cjl          # 本地显示语言包（JSON）
├── problems/problem_N.json
├── articles/article_N.json
├── contests/contest_N.json + contest_N_problem_M_submissions.json
└── submissions/submissions.json
```

旧版平铺在 `data/` 根下的文件会在启动时自动迁移到对应子目录（幂等）。

### 退出码

| 退出码 | 含义 |
|------|------|
| `0` | 成功（查询 / 管理类命令） |
| `1` | 失败 / 对象未找到 / 参数错误 |
| `0` / `1` | `problem submit`、`submit rejudge`：`0` = AC，`1` = 非 AC 或失败 |
| `0` / `1` | `contest problem submit`：`0` = 提交已受理（不看评测结果），`1` = 比赛或题号无效 |
| n | `ide run`：返回被运行程序自身的退出码（沙箱启动失败为 `1`） |

### 评测状态

| 缩写 | 含义 | 缩写 | 含义 |
|------|------|------|------|
| `AC` | Accepted | `PE` | Presentation Error |
| `WA` | Wrong Answer | `OLE` | Output Limit Exceeded |
| `TLE` | Time Limit Exceeded | `PC` | Partially Correct |
| `MLE` | Memory Limit Exceeded | `IV` | Invalid Special Judge |
| `RE` | Runtime Error | `ST` | Special Judge TLE |
| `CE` | Compilation Error | `SR` | Special Judge Runtime Error |
| `SE` | System Error | `IE` | Interactor Error |
| `SK` | Skipped | `??` | Unknown |

### 语言与编译

评测语言由提交文件的扩展名决定：

| 扩展名 | 编译 / 运行 | 说明 |
|------|------|------|
| `.cpp` `.cc` `.cxx` | `g++ -O2 [-static] -o program` | Linux 评测需安装 g++ |
| `.c` | `gcc -O2 [-static] -o program` | |
| `.py` | `python3`（Windows 为 `python`） | 免编译，按 PATH 解析解释器 |
| `.java` | `javac -d <workdir>` → `java -cp <workdir> <MainClass>` | JVM 虚拟内存占用大，内存限力建议 ≥ 2048MB |
| `.js` | `node` | 免编译 |
| 其它 | 自定义语言（见下） | 未注册的扩展名评测为 `CE`（Unsupported language） |

- 仅 `answers_only` 题型接受**目录**或 **`.zip`** 提交（逐文件比对同名答案）。
- 编译器本身在可信模式下运行，超时上限 `judge.compile_time_limit_ms`（默认 10000ms）；选手代码在沙箱中执行。
- 沙箱：Windows 使用 Job Object + 受限令牌；Linux 使用 user/mount/pid/net/uts/ipc 命名空间 + seccomp 系统调用过滤 + 只读根文件系统 + 环境变量白名单 + 子进程数上限（超出后 SIGKILL 击杀）。

#### 自定义语言（`judge.custom_languages`）

内置语言之外的扩展名（如 C#、Kotlin 或自研语言）可在 `data/config.json` 注册后评测：

```json
{
  "judge": {
    "custom_languages": {
      "csharp":    { "extensions": [".cs"], "compile": "csc /nologo /out:{exe} {src}", "run": "{exe}" },
      "judgelang": { "extensions": [".jlang", ".judgelang"], "compile": "", "run": "judgelang {src}" }
    }
  }
}
```

- `extensions` 关联扩展名（自动小写、补 `.`）；`run` 必填，为运行命令模板；`compile` 为编译命令模板，**留空 = 脚本语言**（跳过编译直接执行 `run`）。无效条目（无扩展名或无 `run`）被忽略。
- 模板占位符：`{src}` 选手源文件绝对路径（多源文件各自独立成参）、`{exe}` 编译产物路径（`<workDir>/program<exeSuffix>`）、`{dir}` 编译工作目录绝对路径。
- 模板按空白分词（可用双引号包裹含空格的片段），参数直传沙箱、**不经 shell**；编译命令与内置编译器一样以可信模式运行，受 `compile_time_limit_ms` 约束；`run` 引用了 `{exe}` 但产物缺失 → CE `Compilation produced no output`。
- 命令可为 PATH 上的裸名（如 `python3`、`judgelang`，沙箱环境白名单保留 PATH）或绝对路径；**内置扩展名优先**，与自定义注册冲突时内置生效。
- `custom_languages` 键**缺失**时自动内置示例语言 `judgelang`（`.jlang` / `.judgelang`，运行 `judgelang {src}`，需自行在 PATH 上提供同名解释器，否则提交判 `SE`）；显式给出该键（包括空对象 `{}`）则完全以配置为准。
- 编译器按扩展名识别源语言的（如 g++/gcc），需自行在 `compile` 中指定语言，如 `g++ -O2 -x c++ -o {exe} {src}`。
- `clijudge ide run` 同样支持已注册的自定义语言（编译 + 运行均**不经 shell**，与内置语言一致按结构化 argv 直接执行）。

### 比较模式（`-compare`）

| 模式 | 语义 |
|------|------|
| `text_strict` | 逐字节严格比较（CRLF/LF 归一化），默认 |
| `text_line` | 逐行比较（忽略行末空白）；答案还有内容而选手先结束 → WA，反之 → OLE |
| `text_no_space` | 忽略空白按 token 比较；token 全等但行结构不同 → PE，多余内容 → OLE |
| `float_abs` | 实数绝对误差（超出 `-float-abs` 判错） |
| `float_rel` | 实数相对误差（相对标准答案） |
| `float_all` | 综合误差：`\|Δ\|>absTol` 且 `\|Δ\|>relTol*\|标准\|` 才判错；未设容差默认 1e-6 |
| `spj` | 自定义 SPJ：argv = (输入, 选手输出, 标准输出)，exit 0 → AC 否则 WA，超时 → ST |
| `spj_lemon` | LemonLime 6 参数 SPJ（额外 fullScore/scoreFile/msgFile），按分值判 AC/PC/WA |
| `spj_testlib` | testlib SPJ：解析 stderr 的 `ok` / `partially correct (N)` / `points X` 等 |

非 `text_strict` 的比较模式会自动把题型置为 `traditional`（`-type` 在其后解析，可覆盖）。

### 子任务

- 计分聚合（`-subtask-mode`）：`simple` / `group` = 求和；`all_or_nothing` = 取最小分。
- 依赖（`-dependence`，JSON 如 `{"2": [1]}`）：依赖子任务得 0 或 SKIPPED → 被依赖者 SKIPPED；依赖部分得分 → 按比例封顶。

### 评测设置（`data/config.json` 的 `"judge"` 段）

| 键 | 默认 | 说明 |
|------|------|------|
| `compile_time_limit_ms` | 10000 | 编译超时 |
| `special_judge_time_limit_ms` | 10000 | SPJ / 交互进程超时 |
| `source_size_limit_kb` | 50 | 源代码大小上限 |
| `rejudge_times` | 1 | 边界 TLE 自动重评测次数（0..12） |
| `max_rejudge_times` | 12 | 手动 rejudge 允许的最大评测次数 |
| `max_judging_threads` | 4 | 并行评测线程数 |
| `extra_time_ratio` | 0.1 | 额外时间比（贴线 TLE 缓冲） |
| `file_write_limit_kb` | 16384 | 子进程写文件大小上限 |
| `env` | `{}` | 追加给子进程的环境变量 |
| `custom_languages` | 内置 `judgelang` | 自定义评测语言注册表（见「语言与编译 → 自定义语言」） |

### 显示语言

语言包为 `data/langs/<名称>.cjl`（JSON），当前语言记录在 `config.json` 的 `current_lang`。首次运行自动配置内置英文包（离线可用）；若当前语言文件缺失，除 `help` / `displaylang` 外所有命令报错并提示 `clijudge displaylang switch [langname]`。联网下载在 Linux / Android 下由内置 HTTP(S) 客户端完成，无需外部命令。

---

## 1. 题目模块 `problem`

### clijudge problem count — 统计题目数量

**输出：** 纯数字

```
3
```

### clijudge problem create [标题] [选项...] — 创建题目

**选项：**

| 选项 | 参数 | 说明 |
|------|------|------|
| `-background` / `-describe` | Markdown 文件路径 | 题面，读取文件内容写入 `description`（两者同时给出时后者生效） |
| `-exampleio` | 输入文件 输出文件 | 样例输入 / 输出 |
| `-instyle` / `-outstyle` | Markdown 文件路径 | 输入 / 输出格式说明 |
| `-compare` | 比较模式 | 见上方比较模式表 |
| `-spj-code` | 源文件路径 | Special Judge 源码（内嵌 `spj_code`） |
| `-spj-exe` | 可执行文件路径 | 已编译的 SPJ 可执行文件（`special_judge_exe`） |
| `-float-abs` / `-float-rel` | 正实数 | 浮点容差（大于 0 才生效） |
| `-type` | `traditional` / `answers_only` / `interaction` / `communication` / `communication_exec` | 题型 |
| `-answer-ext` | 扩展名（默认 `out`） | `answers_only` 期望答案文件扩展名 |
| `-source-name` | 文件名 | 选手源文件名（多文件题型） |
| `-subtask-mode` | `simple` / `group` / `all_or_nothing` | 子任务计分聚合 |
| `-dependence` | JSON，如 `{"2": [1]}` | 子任务依赖 |
| `-interactor` | `.cpp/.c` 源码或可执行文件 | 交互题交互器（源码内嵌，否则记录绝对路径） |
| `-grader` | 目录 | 递归内嵌目录下全部文件到 `grader_files` |
| `-generator` | `.cpp/.c` 源码或程序 | 测试点生成器（源码内嵌 `generator_code`，否则 `generator_exe` 绝对路径） |
| `-generator-exe` | 程序路径 | 直接指定生成器可执行文件 |

**示例：**

```bash
clijudge problem create "A + B Problem" -describe statement.md -exampleio in.txt out.txt
clijudge problem create "浮点求和" -compare float_all -float-abs 1e-6 -type answers_only
```

**输出：**

```
Problem created with ID: 1
```

选项解析失败（JSON 非法 / 文件不存在等）时打印错误并返回 `1`，已创建的题目会被回滚删除。

### clijudge problem edit [编号] [选项...] — 编辑题目

选项与 `create` 完全相同，另加 `-title [新标题]`。

**输出：** `Problem 1 updated.`；未提供任何选项时 `No updates specified.`（退出码 1）。

### clijudge problem delete [编号] — 删除题目

**输出：** `Problem 1 deleted.` / 错误 `Problem 1 not found.`（退出码 1）

### clijudge problem view [编号] — 查看题目

格式化输出题面：`=== Problem N ===`、Title、Description / Input / Output、样例（`## Sample Input/Output`）、Hint、`## Limits`（时间 / 内存 / 公开 / 隐藏）、非默认题型或比较模式时的 `## Special Judge` 段。SPJ 为内嵌源码时仅显示 `Source: embedded (N bytes)`，不直接输出代码；可执行文件显示 `Special Judge Exe:` 路径。不输出生成器信息。

### clijudge problem list [L=1] [R=50] — 列出题目

**输出：** 题目摘要 JSON 数组，编号区间 `[L, R]`。

### clijudge problem submit [编号] [程序文件] [--as 用户名] — 提交评测

按扩展名选语言编译运行，结果写入提交记录。

**输出：**

```
=== Submission Result ===
Status: AC
Score: 100
Time: 15 ms
Memory: 3200 KB
User: alice
```

**退出码：** AC 为 `0`，否则 `1`。

### clijudge problem testdata [编号] [子命令] — 测试数据管理

无子命令时等价于 `list`。

#### list — 列出测试点

**输出：** 测试点 JSON 数组（`id` / `score` / `time_limit` / `memory_limit` / `subtask_id` / 输入输出数据等）。

#### create [in] [out] [time] [mem] [pts] — 添加测试点

- `in` / `out` 为文件路径；传 `-` 表示占位测试点（内容由生成器在每次评测前生成 `data.in` / `data.out`）。
- `time`（ms）与 `mem`（MB）省略或为 `-1` 时，评测时回落到题目级 `time_limit` / `memory_limit`（再缺省为 1000ms / 256MB）。
- `pts` 默认 `25`。新测试点归属子任务 1。

```bash
clijudge problem testdata 1 create data.in data.out 1000 256 10
clijudge problem testdata 1 create - -          # 生成器占位测试点
```

**输出：** `Test case created with ID: 1`

#### -set-all [-time ms] [-mem MB] [-pts n] — 批量修改所有测试点

仅覆盖给出且 ≥ 0 的项。

**输出：** `All test cases updated.`

#### -zip [zip路径] — 从 ZIP 导入测试数据

配对 zip 内的 `*.in` 与**同名** `*.out` / `*.ans`，按文件名导入（**追加**，不清空已有测试点）；无配对的 `.in` 跳过并告警。

**输出：**

```
Test case imported: 1 (ID: 4)
Imported 3 test case(s).
```

#### delete [测试点编号] — 删除测试点

**输出：** `Test case 1 deleted.` / `Test case 1 not found.`（退出码 1）

### clijudge problem export [编号] [路径] — 导出题目

按输出路径扩展名选择格式：

- `.zip`：`problem.json` + `testdata/*.in` / `*.out`
- 其它：单个 JSON 文件（`version: 1`，去除 `id`，测试点命名 `case_N`）

两种格式均兼容 NoldOJ 导入格式。

**输出：** `Problem exported to: backup.zip`

### clijudge problem import [路径] — 导入题目

接受 `.zip`（读取 `problem.json` 并解析 testdata 引用）或 `.json`（含 `problem` + `test_cases`，兼容 NoldOJ `version` 格式与 CliJudge 原生格式）。导入时重新分配编号。

**安全提示：** 题目包可能携带 Special Judge / 交互器 / 生成器 / grader 源码，这些代码在评测期间以**可信（放宽隔离）模式**执行——保留时间/内存/进程限制，但跳过受限令牌、命名空间与 seccomp 等隔离层，并可写工作目录。检测到此类组件时会输出 `Warning: ... Only import packages from sources you trust.`；请只导入可信来源的题目包。

**输出：** `Problem imported with ID: 5`

---

## 2. 提交记录模块 `submit`

### clijudge submit count — 统计提交数量

**输出：** 纯数字。

### clijudge submit list [L=1] [R=50] — 列出提交记录

**输出：** JSON 数组，字段含 `id`、`problem_id`、`problem_title`、`file_path`、`username`、`status`、`score`、`time_used`、`memory_used`、`judge_times`、`submitted_at` 等。

### clijudge submit rejudge [提交编号] — 重新评测

重新读取原提交文件评测并更新记录。受 `judge.max_rejudge_times`（默认 12）限制；源文件必须仍存在。

**输出：**

```
=== Rejudge Result ===
Submission: 1
Status: AC
Score: 100
Time: 15 ms
Memory: 3200 KB
Judge times: 2
```

**退出码：** AC 为 `0`，否则 `1`；超限时 `Max rejudge times reached (12).`（退出码 1）。

---

## 3. 比赛模块 `contest`

### clijudge contest create [标题] [开始时间] [结束时间] [题目1] [题目2...] — 创建比赛

时间字符串原样存储，无格式校验，建议使用 `YYYY-MM-DD HH:MM:SS`。题目编号按给出顺序成为比赛内题号（1 起）。

```bash
clijudge contest create "寒假集训" "2026-01-01 10:00:00" "2026-01-01 12:00:00" 1 2
```

**输出：** `Contest created with ID: 1`

### clijudge contest delete [编号] — 删除比赛

**输出：** `Contest 1 deleted.` / `Contest 1 not found.`（退出码 1）

### clijudge contest view [编号] — 查看比赛

**输出：** 比赛详情 JSON（`id` / `title` / `start_time` / `end_time` / `problem_ids` / `created_at`）。

### clijudge contest list — 列出比赛

**输出：** 比赛摘要 JSON 数组。

### clijudge contest problem [比赛编号] [题号] submit [文件] [--as 用户名] — 比赛内提交

立即评测并把结果（`result` / `score` / `time_used` / `memory_used` / `judge_detail`）写入该比赛该题的提交记录。

**输出：**

```
Submission accepted for contest 1 problem 2
  User: alice
```

**退出码：** 提交成功即 `0`（不看评测结果）；比赛或题号无效为 `1`。

### clijudge contest problem [比赛编号] [题号] view — 查看比赛题目提交

**输出：** 该题提交记录 JSON 数组。

### clijudge contest leaderboard [比赛编号] — 比赛排名

**输出：** JSON 数组，每项 `{ "username", "score", "accepted", "total" }`，按总分降序（AC 提交计入 AC 数；非 AC 也累加得分）。

### clijudge contest report [比赛编号] [输出.html] — 导出比赛报告

生成含排名、题目、提交明细（含 `judge_detail`）的 HTML。输出路径缺省为 `contest_<编号>_report.html`。

**输出：** `Contest report generated: contest_1_report.html`

### clijudge contest import [cdf路径] — 导入 CDF 比赛

CDF 为兼容 LemonLime 的比赛数据 JSON（题目、测试数据、提交、排行）。逐题导入本地题目库后创建比赛。

**安全提示：** CDF 可能携带 Special Judge / 交互器 / grader 源码（评测期以可信/放宽隔离模式执行），检测到时会输出 `Warning: ... Only import contests from sources you trust.`；请只导入可信来源的 CDF。

**输出：**

```
  Imported problem: A + B Problem (ID: 1)
Contest imported: 寒假集训 (ID: 1)
```

### clijudge contest export [比赛编号] [cdf路径] — 导出 CDF 比赛

**输出：** `Contest exported to: contest.cdf`（紧凑 JSON）

---

## 4. 文章模块 `article`

### clijudge article count — 统计文章数量

**输出：** 纯数字。

### clijudge article create [标题] [md文件路径] — 创建文章

md 文件可省略（正文为空）。**`article` 无 edit 子命令**，事后只能直接手改 `data/articles/article_N.json` 或删除重建。

**输出：** `Article created, ID: 1`

### clijudge article delete [编号] — 删除文章

**输出：** `Article 1 deleted.` / `Article 1 not found.`（退出码 1）

### clijudge article list [L=1] [R=50] — 列出文章

**输出：** 文章摘要 JSON 数组。

### clijudge article view [编号] — 查看文章

**输出：** 文章详情 JSON（标题、Markdown 正文等）。

---

## 5. 在线运行模块 `ide`

### clijudge ide run [代码路径] [输入文件路径] — 运行代码

在沙箱中编译并运行。编译复用评测链路（可信沙箱、编译时限配置、错误输出捕获后打印）；随后以**单进程**在受限沙箱中运行（超时 10s、内存 256MB、进程上限 1，不经 shell，结构化 argv 直接执行），程序输出直接打印到终端；提供输入文件时重定向其内容到 stdin。临时工作目录运行后自动清理。

支持语言：C/C++（`.cpp` `.cc` `.cxx` `.c`）、Python（`.py`）、Java（`.java`）、JavaScript（`.js`）；其它扩展名尝试直接执行。

```bash
clijudge ide run main.cpp in.txt
```

**输出：**

```
Exit code: 0
Time: 15 ms
Memory: 3200 KB
```

被信号终止时追加一行 `Signal: SIGKILL`。

**退出码：** 程序自身退出码；沙箱启动失败为 `1`。

---

## 6. 显示语言模块 `displaylang`

### clijudge displaylang list [--online] — 列出语言

本地列表每项为 `名称 (显示名) v版本 *`（`*` 为当前语言）；`--online` 列出远端可下载语言。

**输出：**

```
Local languages:
  en (English) v1.0.0 *

* = currently active
```

### clijudge displaylang switch [语言名] — 切换语言

本地没有该语言包时自动下载（`en` 为内置包，离线可用）。

**输出：** `Switched to language: zh`

### clijudge displaylang delete [语言名] — 删除本地语言

若删除的是当前语言，先停用（`Deactivated current language.`）再删除。内置语言 `en` 拒绝删除（exit 1，`Built-in language 'en' cannot be deleted.`；刷新本地副本用 `displaylang pull en`）。

**输出：** `Deleted language: zh`

### clijudge displaylang pull [语言名] — 拉取语言包（不切换）

**输出：**

```
Downloading language: zh...
Updated language: zh
```
