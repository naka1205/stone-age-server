# tests/CMakeLists.txt
#
# ★ doctest 已接入(DR-TS4)。接入时机由 **DR-TS6** 提前:
#   原计划「阶段 1.1 有实现后再接」,但 DR-TS6 裁定 0.1 一遍到位、放弃两段式后,
#   **黄金用例集从「建议做」升级为「与移植同步进行的硬要求」**
#   —— 它是 `00` §0 中 ③ 层「规则能写出来但无法自证正确」的主要补偿手段。
#   ⇒ 必须在移植第一个函数**之前**就位,不能等移植完再补。
#
# ⚠️ doctest 只进 tests/,**不进 shared/** —— 客户端以 FetchContent 引用 shared/ 时
#    不应被迫拉一个测试框架(`01` §4「shared/ 只依赖标准库」)。

include(FetchContent)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE
    # ⚠️★ 故意**只取源码,不 add_subdirectory**。
    #
    # 理由是实测:doctest v2.4.11 的顶层 CMakeLists 写着
    # `cmake_minimum_required(VERSION 3.0)`,而新版 CMake 已移除对 < 3.5 的兼容
    # ⇒ 让它参与配置会直接失败。
    #
    # ★ 但这对本项目**不构成问题** —— DR-TS4 选 doctest 的理由第一条就是
    #   「**单头文件**、编译开销低」。我们要的就是那一个头,不是它的构建系统。
    #   这同时避免了它的 install/export 规则污染本项目的安装面。
    #
    # ★ 实现手法(2026-09-02 改):SOURCE_SUBDIR 指向一个**不存在的子目录**,
    #   MakeAvailable 找不到 CMakeLists.txt 就只下载、不 add_subdirectory。
    #   这是 CMake 官方给的「只填充」写法。
    #   ⚠️ 原先用的 `FetchContent_Populate(doctest)` 在 CMake 4.x 已废弃,
    #      且官方明说**将来会完全移除**(CMP0169)⇒ 那是一颗定时炸弹,不是风格问题。
    #      客户端仓同处已同步改。
    SOURCE_SUBDIR  sg-do-not-configure)
FetchContent_MakeAvailable(doctest)

add_library(sg_doctest INTERFACE)
target_include_directories(sg_doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")
# ★ SYSTEM:第三方头不参与本项目的告警口径(cmake/SgWarnings.cmake 开了
#   -Wconversion / -Wold-style-cast 等,doctest 自己并不按这套写)。

# ── 契约层冒烟 ───────────────────────────────────────────────────
#
# ★ 故意用 **C++17** 编译:GameStudio 是 C++17,shared/ 必须在它下面能编译,
#   否则 D2「一份规则两端编译」在客户端侧当场失败。
#   ⇒ 让不兼容在服务端构建时就暴露,而不是等到客户端集成那天。
add_executable(sg_contract_smoke contract_smoke.cpp)
target_link_libraries(sg_contract_smoke PRIVATE sg_shared)
set_target_properties(sg_contract_smoke PROPERTIES
    CXX_STANDARD ${SG_SHARED_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
sg_apply_warnings(sg_contract_smoke)
sg_enable_assertions(sg_contract_smoke)   # ★ 见 cmake/SgWarnings.cmake:防「绿色的假测试」
add_test(NAME contract_smoke COMMAND sg_contract_smoke)

# ── ★★ L3 黄金用例集(阶段 0.1/1.1 批次 0)────────────────────────
#
# 它承担两件事,缺一不可:
#   ① **回归**:同种子 + 同输入 ⇒ 逐位相同(可回放,`01` §10);
#   ② **溯源**:每条断言指到 `battle_event.c` / `battle.c` 的行号或一条 DR。
#
# ⚠️ 同样锁 C++17 —— 用例集也要能在客户端工具链下跑,否则"双端同一份规则"
#    只有实现是共享的、验证不是。
add_executable(sg_rules_battle_test rules_battle_test.cpp)
target_link_libraries(sg_rules_battle_test PRIVATE sg_shared sg_doctest)
set_target_properties(sg_rules_battle_test PROPERTIES
    CXX_STANDARD ${SG_SHARED_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
sg_apply_warnings(sg_rules_battle_test)
sg_enable_assertions(sg_rules_battle_test)
add_test(NAME rules_battle COMMAND sg_rules_battle_test)

# ── IDL 生成物冒烟(与 idl/tests/run.sh 同源)──────────────────────
#
# 这个用 C++20:生成物只被服务端与客户端的**网络层**消费,不进 shared/。
add_executable(sg_idl_smoke ${CMAKE_SOURCE_DIR}/idl/tests/smoke.cpp)
target_link_libraries(sg_idl_smoke PRIVATE sg_idl)
sg_apply_warnings(sg_idl_smoke)
sg_enable_assertions(sg_idl_smoke)        # ★ 同上
add_test(NAME idl_smoke COMMAND sg_idl_smoke)

# ── ★★ shared/ 依赖纯度(D2 的守卫)───────────────────────────────
#
# 它挡的是 D2 失效,不是代码风格 —— 见 tools/check_shared_purity.py 卷首。
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME shared_purity
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_SOURCE_DIR}/tools/check_shared_purity.py)
  # IDL schema 与生成物同步(02 §8 检查 1–3 + 生成物入库)
  #
  # ⚠️★ 它需要 **protoc**,而 protoc 是**构建期工具**、不是运行时依赖:
  #    DR-TS1 边界 ① 运行时不链接 libprotobuf;DR-TS2 生成物入库
  #    ⇒ **只消费生成物的人根本不需要装 protoc**(客户端连 protoc 都不需要)。
  #    ⇒ 「本机没有 protoc」是一种正常状态,不该表现为一条常红的 ctest ——
  #      常红会训练人忽略红色,而这个仓最怕的恰是「把绿/红读错」(00 §10.4)。
  #
  # ⇒ 按本文件下方「Python3 缺失」的同一先例处理:**不注册 + 大声告警**。
  # ⚠️ 但 CI 上必须有 protoc,否则这道关就**静默消失**了 —— 那比常红更坏。
  find_program(SG_PROTOC NAMES protoc)
  if(SG_PROTOC)
    add_test(NAME idl_verify
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/idl/codegen/sgidl_gen.py --verify)
  else()
    message(WARNING
        "未找到 protoc —— IDL 生成物同步检查(idl_verify)不会注册。\n"
        "   ⚠️ 这不是可选的风格检查:它守的是「schema 改了、生成物没重跑」这类"
        "静默漂移(02 §8 检查 1–3)。**CI 上必须有 protoc。**\n"
        "   本机若只消费生成物则无需安装(DR-TS2 已入库);改 schema 前请先装上,"
        "或设 PROTOC=<路径>。")
  endif()
else()
  message(WARNING
      "未找到 Python3 —— shared/ 依赖纯度与 IDL 同步检查将不会运行。"
      "⚠️ 这两条不是可选的风格检查,CI 上必须有 Python3。")
endif()
