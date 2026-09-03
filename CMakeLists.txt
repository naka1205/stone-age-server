# stone-age-server —— 顶层构建
#
# 依据 01-server-architecture.md §4 的目录结构与 00-architecture.md 的裁定:
#   D1 = C++20 单语言 · D2 = shared/ 双端共享源码 · DR-TS1 = IDL 生成纯 POD
#
# ★★ 本文件承载两条**结构性**约束,不只是构建配置:
#   ① shared/ 只依赖标准库(01 §4)—— 它要被客户端以 FetchContent 方式编译(DR-TS3),
#      一旦掺入 socket / MySQL / 日志,D2 当场失效。
#   ② 模块之间只能通过接口通信,不得有共享内存捷径(00 §3.1)——
#      「进程内形态下也不许直接读对方内部状态」。
#      ⇒ 各模块互相不可见,只暴露接口头,链接期换实现。
#
# ⚠️ 违反 ② 的后果是 00 §10.4 列的三类静默错误之一:
#    **单容器跑通、生产分布式挂**,且是最难查的那种。

cmake_minimum_required(VERSION 3.20)

project(stone_age_server
        VERSION 0.1.0
        DESCRIPTION "石器时代 8.0 重制 —— 服务端"
        LANGUAGES CXX)

# ── C++20,不用编译器扩展 ──────────────────────────────────────────
# D1 裁定 C++20;GameStudio 是 C++17 ⇒ shared/ 的代码必须在 C++17 下也能编译,
# 否则双端共享会在客户端侧失败。见 §shared/CMakeLists.txt 的 SA_SHARED_CXX_STANDARD。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(SaWarnings)

option(SA_BUILD_TESTS "构建测试" ON)

# ── IDL 生成产物 ─────────────────────────────────────────────────
#
# DR-TS2:生成产物**入库**,构建期不跑 protoc、不依赖 libprotobuf
#         ⇒ 客户端连 protoc 都不需要。
# 重新生成是**显式动作**:python3 idl/codegen/saidl_gen.py
#
# ⚠️ 这里故意**不**把 codegen 挂成构建步骤。理由:schema 改动会动协议号,
#    那是需要走 CI 检查 2(编号稳定性)与人工复核的事,不该被一次顺手的
#    `cmake --build` 静默带过。
add_library(sa_idl INTERFACE)
target_include_directories(sa_idl INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/idl/generated/cpp")

# ⚠️★ sa_idl 同时暴露 domain/ 与 transport/ 两组头。
#    **靠 include 路径无法隔离**(生成物的 #include 一律从 generated/cpp 根算起)。
#    ⇒ 「shared/ 不得依赖传输类型」(02 §9)由 tools/check_shared_purity.py 强制,
#      不由 CMake 强制。该脚本是 CI 必跑项,不是可选的风格检查。

add_subdirectory(shared)
add_subdirectory(src)

if(SA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# TODO(阶段 1.1):引入 doctest(DR-TS4 已裁定)。
#   现在 tests/ 用标准 <cassert>,因为契约层的用例数还很少;
#   等 shared/rules 有实现、黄金用例集开始成批录入时再接 FetchContent。
# TODO(阶段 2):src/ 其余模块(storage / lock / gateway /
#   session_storage / social)与 deploy/。
