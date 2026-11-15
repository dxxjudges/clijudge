cmake_minimum_required(VERSION 3.10)
project(CliJudge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 与 README / CI 发布命令保持一致：默认 Release (-O2)
# （CMake 自带的 Release 默认是 -O3，这里覆盖为 CI 使用的 -O2，避免双轨产物漂移）
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# include 目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 单翻译单元构建：miniz 通过 include/miniz_impl.h 随 main.cpp 一并编译
add_executable(clijudge src/main.cpp)

if(WIN32)
  # UTF-8 代码页 manifest（argv/文件路径 UTF-8），随 RC 编译嵌入 exe
  enable_language(RC)
  target_sources(clijudge PRIVATE src/clijudge.rc)
  target_compile_definitions(clijudge PRIVATE WINVER=0x0600 _WIN32_WINNT=0x0600)
  target_link_libraries(clijudge psapi userenv ole32 winhttp)
  # 对齐 CI 的 -static（MinGW）
  set_target_properties(clijudge PROPERTIES LINK_FLAGS "-static")
endif()

install(TARGETS clijudge RUNTIME DESTINATION bin)

# 判题核心单测（纯函数，不依赖编译器/沙箱）：cmake -B build && cmake --build build && ctest --test-dir build
option(CLIJUDGE_BUILD_TESTS "Build judge unit tests" ON)
if(CLIJUDGE_BUILD_TESTS)
  enable_testing()
  add_executable(judge_tests tests/test_judge.cpp)
  if(WIN32)
    target_compile_definitions(judge_tests PRIVATE WINVER=0x0600 _WIN32_WINNT=0x0600)
    target_link_libraries(judge_tests psapi userenv ole32 winhttp)
  endif()
  add_test(NAME judge_tests COMMAND judge_tests)
endif()
