# CliJudge

轻量级命令行在线评测系统（Windows / Linux 双平台，支持 x64 与 ARM64，另支持 Android Termux）

预编译二进制见 [Releases](https://github.com/dxxjudges/clijudge/releases)（`clijudge.exe` / `clijudge-linux.bin`，ARM64 版为 `clijudge-windows-arm64.exe` / `clijudge-linux-arm64.bin`，Termux 版为 `clijudge-android-arm64.bin`），或 `npm install -g clijudge`（Windows / Linux / Termux，安装后直接使用 `clijudge` 命令，自动按架构与平台选择二进制）。

## 功能特性

- **题目管理**: 创建、编辑、删除、导入导出题目；子任务计分、浮点容差、Special Judge 与交互题（interactor / grader）
- **测试点生成器**: 题目可附带生成器（源码内嵌或外部可执行文件），每次评测前按测试点编号运行，写入 `data.in`/`data.out` 作为该测试点的评测数据
- **比赛管理**: 创建比赛、管理比赛题目；LemonLime CDF 导入导出、排行榜与 HTML 比赛报告
- **提交评测**: 双平台沙箱安全执行代码（Windows：Job Object + 受限令牌 + Low IL；Linux：namespaces + seccomp + RLIMIT，加固环境自动降级）
- **多语言评测**: 内置 C / C++ / Python / Java / JavaScript，`custom_languages` 可注册任意扩展名
- **显示语言**: `.cjl` 语言包，`displaylang` 联网切换
- **数据存储**: JSON格式存储，方便导入导出

## 子命令列表

```
├─article
│  ├─count
│  ├─create [标题] [md文件路径]
│  ├─delete [编号]
│  ├─list [L=1] [R=50]
│  └─view [编号]
├─contest
│  ├─create [标题] [开始时间] [结束时间] [题目1] [题目2]
│  ├─delete [编号]
│  ├─export [编号] [cdf路径]          # 导出 LemonLime CDF
│  ├─import [cdf路径]                # 导入 CDF
│  ├─leaderboard [编号]              # 比赛排名
│  ├─list                           # 列出比赛
│  ├─report [编号] [输出.html]        # 导出 HTML 比赛报告
│  ├─problem [编号] [题目在比赛中的编号]
│  │  ├─submit [文件地址] [--as 用户]
│  │  └─view                         # 查看比赛提交
│  └─view [编号]
├─displaylang
│  ├─list [--online]                 # 列出本地/在线语言
│  ├─switch [语言名]                  # 切换显示语言
│  ├─pull [语言名]                    # 拉取语言包（不切换）
│  └─delete [语言名]                  # 删除本地语言
├─ide
│  └─run [代码路径] [in文件路径]       # 即写即跑
├─problem
│  ├─count
│  ├─create [标题]                   # 编辑用 edit [编号]，选项相同
│  │  ├─-type / -compare / -spj-code / -spj-exe
│  │  ├─-float-abs / -float-rel / -subtask-mode
│  │  ├─-answer-ext / -source-name / -dependence
│  │  ├─-interactor / -grader
│  │  ├─-background / -describe / -exampleio
│  │  ├─-instyle / -outstyle
│  │  └─-generator / -generator-exe
│  ├─delete [编号]
│  ├─edit [编号]                     # 另含 -title
│  ├─export [zip路径]
│  ├─import [zip路径]
│  ├─list [L=1] [R=50]
│  ├─submit [编号] [程序文件路径] [--as 用户]
│  ├─testdata [题目编号]
│  │  ├─-set-all
│  │  ├─-zip [zip路径]
│  │  ├─create [in] [out] [time] [mem] [pts]  (in/out 可为 - 占位, 由生成器生成)
│  │  ├─delete [编号]
│  │  └─list
│  └─view [编号]
└─submit
    ├─count
    ├─list [L=1] [R=50]
    └─rejudge [编号]                  # 重新评测
```

## 编译

### 使用CMake

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### 使用g++直接编译

Windows:

```bash
windres src/clijudge.rc -O coff -o clijudge_res.o
g++ -std=c++17 -O2 -Wall -static -I include -o clijudge.exe src/main.cpp clijudge_res.o -lpsapi -luserenv -lole32 -lwinhttp
```

> **Windows Vista 兼容**：`-static` 必须保留，否则运行时会缺少 `libgcc_s_seh-1.dll` / `libstdc++-6.dll` / `libwinpthread-1.dll`。此外 Vista 不含 **UCRT**（`ucrtbase.dll`），需使用 **MSVCRT** 版工具链（winlibs `*-msvcrt` 或 MSYS2 的 MINGW64）；UCRT 版工具链（winlibs UCRT64、MSYS2 UCRT64）产出的 exe 仅支持 Windows 10+。

Linux:

```bash
g++ -std=c++17 -O2 -Wall -I include -o clijudge src/main.cpp
```

Termux（Android，Bionic）:

```bash
pkg install clang
clang++ -std=c++17 -O2 -Wall -I include -o clijudge src/main.cpp
```

Linux / Android 下语言包联网下载由内置 HTTP(S) 客户端完成，无需 `curl` 等外部命令（`displaylang` 命令）。

## 使用方法

```bash
# 查看帮助
clijudge.exe help

# 创建题目
clijudge.exe problem create "A + B Problem"

# 查看题目
clijudge.exe problem view 1

# 列出所有题目
clijudge.exe problem list

# 创建文章
clijudge.exe article create "题目说明"

# 创建比赛
clijudge.exe contest create "比赛标题" "2026-01-01 10:00:00" "2026-01-01 12:00:00" 1 2
```

## 数据存储

数据默认存储在可执行文件同级目录的 `data/` 文件夹下，可通过环境变量 `CLIJUDGE_DATA_DIR` 自定义。题目、文章、比赛、提交分别存放在 `data/problems`、`data/articles`、`data/contests`、`data/submissions` 子目录中；旧版平铺在 `data/` 根下的数据会在启动时自动迁移到对应子目录。

## 系统要求

- Windows Vista 或更高版本 / Linux（glibc）/ Android 7+（Termux，arm64）
- 支持C++17的编译器
- 判题运行环境需 `g++`/`gcc`（编译选手代码；Termux 安装 `clang` 包后自带 g++ 软链接）
- Android 上用户命名空间可能被系统限制，沙箱自动降级为 seccomp + RLIMIT
- Android 上沙箱会剥离 `LD_PRELOAD`（termux-exec 不生效），自定义语言脚本的 shebang 需使用 `$PREFIX` 下的绝对路径（如 `#!/data/data/com.termux/files/usr/bin/bash`）

## 许可证

MIT License