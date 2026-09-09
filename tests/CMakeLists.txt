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

add_library(sa_doctest INTERFACE)
target_include_directories(sa_doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")
# ★ SYSTEM:第三方头不参与本项目的告警口径(cmake/SaWarnings.cmake 开了
#   -Wconversion / -Wold-style-cast 等,doctest 自己并不按这套写)。

# ── 契约层冒烟 ───────────────────────────────────────────────────
#
# ★ 故意用 **C++17** 编译:GameStudio 是 C++17,shared/ 必须在它下面能编译,
#   否则 D2「一份规则两端编译」在客户端侧当场失败。
#   ⇒ 让不兼容在服务端构建时就暴露,而不是等到客户端集成那天。
add_executable(sa_contract_smoke ContractSmoke.cpp)
target_link_libraries(sa_contract_smoke PRIVATE sa_shared)
set_target_properties(sa_contract_smoke PROPERTIES
    CXX_STANDARD ${SA_SHARED_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
sa_apply_warnings(sa_contract_smoke)
sa_enable_assertions(sa_contract_smoke)   # ★ 见 cmake/SaWarnings.cmake:防「绿色的假测试」
add_test(NAME contract_smoke COMMAND sa_contract_smoke)

# ── ★★ L3 黄金用例集(阶段 0.1/1.1 批次 0)────────────────────────
#
# 它承担两件事,缺一不可:
#   ① **回归**:同种子 + 同输入 ⇒ 逐位相同(可回放,`01` §10);
#   ② **溯源**:每条断言指到 `battle_event.c` / `battle.c` 的行号或一条 DR。
#
# ⚠️ 同样锁 C++17 —— 用例集也要能在客户端工具链下跑,否则"双端同一份规则"
#    只有实现是共享的、验证不是。
add_executable(sa_rules_battle_test RulesBattleTest.cpp)
target_link_libraries(sa_rules_battle_test PRIVATE sa_shared sa_doctest)
set_target_properties(sa_rules_battle_test PROPERTIES
    CXX_STANDARD ${SA_SHARED_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
sa_apply_warnings(sa_rules_battle_test)
sa_enable_assertions(sa_rules_battle_test)
add_test(NAME rules_battle COMMAND sa_rules_battle_test)

# ── ★ 成长养成:属性推导黄金用例(DR-DT9,批次 M.3)──────────────────
#
# 与 rules_battle 同源同框架、同锁 C++17:deriveBaseStats 是 shared/rules 的纯函数,
# 双端共编 ⇒ 用例也要能在客户端工具链下跑。四维 → 三围公式逐位钉死(Progression.h)。
add_executable(sa_rules_progression_test RulesProgressionTest.cpp)
target_link_libraries(sa_rules_progression_test PRIVATE sa_shared sa_doctest)
set_target_properties(sa_rules_progression_test PROPERTIES
    CXX_STANDARD ${SA_SHARED_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
sa_apply_warnings(sa_rules_progression_test)
sa_enable_assertions(sa_rules_progression_test)
add_test(NAME rules_progression COMMAND sa_rules_progression_test)

# ── ★★ L2 实体池地基(阶段 2 起步)────────────────────────────────
#
# 守 03-domain-model.md §9 的 M2 / M10 与 §8.2「索引不得线性扫描」在**结构上**成立:
# 定长池 + generation 句柄回收(悬空 ⇒ nullptr,不脏读)+ 标识符→句柄 O(1) 索引。
#
# ⚠️ 同锁 C++17 —— shared/model 与 shared/rules 一样要能在客户端工具链(C++17)下编译。
add_executable(sa_model_pool_test ModelPoolTest.cpp)
target_link_libraries(sa_model_pool_test PRIVATE sa_shared sa_doctest)
set_target_properties(sa_model_pool_test PROPERTIES
    CXX_STANDARD ${SA_SHARED_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
sa_apply_warnings(sa_model_pool_test)
sa_enable_assertions(sa_model_pool_test)
add_test(NAME model_pool COMMAND sa_model_pool_test)

# ── IDL 生成物冒烟(与 idl/tests/run.sh 同源)──────────────────────
#
# 这个用 C++20:生成物只被服务端与客户端的**网络层**消费,不进 shared/。
add_executable(sa_idl_smoke ${CMAKE_SOURCE_DIR}/idl/tests/smoke.cpp)
target_link_libraries(sa_idl_smoke PRIVATE sa_idl)
sa_apply_warnings(sa_idl_smoke)
sa_enable_assertions(sa_idl_smoke)        # ★ 同上
add_test(NAME idl_smoke COMMAND sa_idl_smoke)

# ── ★ 1.5 最小 L0/L1 的用例(net / platform / world)──────────────
#
# ⚠️ 这三个用 **C++20**(D1),不锁 C++17 —— 它们测的是 src/ 各模块,
#    那是服务端独有的东西,不参与 D2 的双端共享。
#    ⇒ 只有 shared/ 与它的用例集才受 GameStudio 的 C++17 约束。
# CTest / target 名保持 snake_case(文档与记忆按此 ID 引用);源文件名已 P5 Pascal 化。
foreach(sa_t IN ITEMS net_framing net_tcp platform_config world_tick)
  # snake_case 基名 → PascalCase 源文件名(net_framing → NetFramingTest.cpp)
  string(REPLACE "_" ";" _parts "${sa_t}")
  set(_src "")
  foreach(_p IN LISTS _parts)
    string(SUBSTRING "${_p}" 0 1 _head)
    string(TOUPPER "${_head}" _head)
    string(SUBSTRING "${_p}" 1 -1 _tail)
    string(APPEND _src "${_head}${_tail}")
  endforeach()
  add_executable(sa_${sa_t}_test ${_src}Test.cpp)
  sa_apply_warnings(sa_${sa_t}_test)
  sa_enable_assertions(sa_${sa_t}_test)
  add_test(NAME ${sa_t} COMMAND sa_${sa_t}_test)
endforeach()
target_link_libraries(sa_net_framing_test      PRIVATE sa_net sa_doctest)
target_link_libraries(sa_platform_config_test  PRIVATE sa_platform sa_doctest)
target_link_libraries(sa_world_tick_test       PRIVATE sa_world sa_doctest)

# ── ★ TcpTransport 的用例要自己开 socket(2026-09-04)─────────────
#
# ★★ 它比其它三个多两笔链接,两笔都有理由:
#   ① **ws2_32** —— sa_net 是 PRIVATE 链接它的(见 src/net/CMakeLists.txt:
#     不让 winsock 的头与宏顺着依赖传染给 world 与其它测试)。
#     ⇒ 本用例自己用裸 socket 当**测试客户端**,就得自己链一次。
#     ⚠️ 这不是重复,恰恰是那条 PRIVATE 生效的证据。
#   ② **Threads** —— 大负载往返那条用例要在另一个线程收,
#     否则服务端要 Poll 才继续写、而 Poll 在同一线程上,必然死锁。
#     ⚠️ Linux 上漏了它是**链接期**报错,mac 上却能过 —— 又一处
#       「在一套工具链上验过 ≠ 三套都成立」(00 §9.0.6)。
find_package(Threads REQUIRED)
target_link_libraries(sa_net_tcp_test PRIVATE sa_net sa_doctest Threads::Threads)
if(WIN32)
  target_link_libraries(sa_net_tcp_test PRIVATE ws2_32)
endif()

# ── ★ 入口自检(2026-09-05,TcpTransport 接入入口时补)─────────────────
#
# main.cpp 此前**没有任何测试覆盖** —— 它是"产物",不开断言防线(src/CMakeLists.txt),
# 而模块各自的用例都绕开它。接上真 socket 之后,入口多了三件只有它自己会做错的事:
# 绑端口失败要拒绝启动、tick 循环要能停、Stop 与 World 的析构顺序不能反。
# ⇒ 让 --self-test 成为 ctest 的一项:启动 → 绑系统分配的端口 → 跑够一个回合间隔
#   → 关闭 → 退出码 0。它证明的是「入口能启动、能监听、能停」,不是某个端口空着
#   (端口取 0 正是为了在 CI 上并行跑而不撞端口,见 main.cpp 的用法说明)。
# ⚠️ 与客户端 `--autotest` 同一取向:一切生成物须可无人验证。
add_test(NAME server_self_test COMMAND stone_age_server --self-test)

# ── ★★ shared/ 依赖纯度(D2 的守卫)───────────────────────────────
#
# 它挡的是 D2 失效,不是代码风格 —— 见 tools/check_shared_purity.py 卷首。
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME shared_purity
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_SOURCE_DIR}/tools/check_shared_purity.py)
  # ★★ 模块边界(00 §3.1 的守卫)—— 与上一条同族:
  #    那一条守 D2(shared/ 不依赖服务端设施),这一条守「进程内捷径」
  #    (00 §10.4 三类静默错误里最难查的一类:单容器跑通、生产分布式挂)。
  add_test(NAME module_boundaries
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_SOURCE_DIR}/tools/check_module_boundaries.py)
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
  find_program(SA_PROTOC NAMES protoc)
  if(SA_PROTOC)
    add_test(NAME idl_verify
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/idl/codegen/saidl_gen.py --verify)
  else()
    message(WARNING
        "未找到 protoc —— IDL 生成物同步检查(idl_verify)不会注册。\n"
        "   ⚠️ 这不是可选的风格检查:它守的是「schema 改了、生成物没重跑」这类"
        "静默漂移(02 §8 检查 1–3)。**CI 上必须有 protoc。**\n"
        "   本机若只消费生成物则无需安装(DR-TS2 已入库);改 schema 前请先装上,"
        "或设 PROTOC=<路径>。")
  endif()

  # ── ★★ .clang-format 纪律的执行者(2026-09-08,01 §13 欠债 24)──────
  #
  # P6(00 §9.0.23)宣告「五仓字节一致、CI --Werror 强制」,⚠️ 而在本条落地之前
  # **没有任何东西检查它** ⇒ 那句「强制」从来没有执行者,HEAD 上实测已漂到
  # 10 处 / 5 个文件,而 ctest 全程 13/13 全绿。
  # ★ 与 shared_purity / module_boundaries 同族:它守的不是"好看",
  #   是「一个已宣告完成的状态还成不成立」。
  #
  # ⚠️ 按 protoc 的同一先例处理:**缺工具不注册 + 大声告警**,而不是常红
  #    (常红会训练人忽略红色,00 §10.4)。
  #    ⇒ CI 上必须有 clang-format,接住这件事的是 ci_verify.py §2 的
  #      EXPECTED_TESTS 断言(code_format 少了就硬失败),不是这里。
  # ── ★★ DR 登记表的可数性(批次 M.5,00 §9.0.35 ⑦)──────────────────
  #
  # `11` §14 的口径是「要问还有几条没定,就数 §1–§12 里的 ⚠️ 与 ⏳ 标记」
  # ⇒ 那句话架在一个前提上:**每条 DR 都在主表里有一行**。
  # ⚠️ 而该前提此前没有执行者 —— 实测三条 DR 只有展开小节、主表无行
  #   (DR-DT11 / DR-BT22 / DR-DT12),且那个行数**已经记错三次**。
  # ★★ 后果不是格式:若某条新 DR 是 ⚠️ 待拍板却只写在小节里,
  #   「待拍板 0 条」会是错的**而且不会有任何东西发现** ——
  #   而"没有决策挡住开工"这个判断正是建立在那个数上的。
  #
  # ★ 与 code_format 不同,本检查**只读仓内文档、不依赖任何外部工具**
  #   ⇒ 无条件注册,不需要"缺工具则不注册 + 告警"那一套。
  add_test(NAME dr_table
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_SOURCE_DIR}/tools/check_dr_table.py)

  find_program(SA_CLANG_FORMAT
      NAMES clang-format
      # ⚠️★★ 必须是 PATHS 而**不是** HINTS —— 这不是同义写法:
      #   CMake 的搜索次序是 … → HINTS → 系统 PATH → PATHS,
      #   ⇒ 写成 HINTS 就会**优先于 PATH**,而 CI 恰恰是用 pip 往 PATH 上
      #     装了一个**钉死版本**的 clang-format(见 .github/workflows/ci.yml)。
      #   在 macOS runner 上 Xcode 工具链那个路径是存在的 ⇒ HINTS 会让 CMake
      #   挑中它、把 CI 的版本 pin **整个绕过**,而没有任何东西会报错。
      #   ★ 与 00 §9.0.12 那族「开关静默失效」同形态。
      #   ⇒ 语义要的是「先用 PATH 上的;本机没有再退回这些已知位置」= PATHS。
      PATHS "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin"
            "/opt/homebrew/opt/llvm/bin"
            "/usr/local/opt/llvm/bin")
  if(SA_CLANG_FORMAT)
    add_test(NAME code_format
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/check_format.py)
    # ★ 把工具路径传给脚本:CMake 已经找到了它(含上面那些 HINTS),
    #   不该让脚本再独立找一遍 —— 两处各找一遍就会出现「ctest 绿而
    #   手工跑红」这种用的不是同一个二进制的分叉。
    set_tests_properties(code_format PROPERTIES
        ENVIRONMENT "SA_CLANG_FORMAT=${SA_CLANG_FORMAT}")
  else()
    message(WARNING
        "未找到 clang-format —— 格式检查(code_format)不会注册。\n"
        "   ⚠️ 它守的是 P6 宣告的「三仓单一格式体系」(00 §9.0.23 / DR-TS7),"
        "而该纪律在本检查之前**没有任何执行者**(01 §13 欠债 24)。\n"
        "   **CI 上必须有 clang-format**;本机安装:brew install llvm  或\n"
        "   python3 -m pip install clang-format==21.1.8,或设 "
        "-DSA_CLANG_FORMAT=<路径>。")
  endif()
else()
  message(WARNING
      "未找到 Python3 —— shared/ 依赖纯度与 IDL 同步检查将不会运行。"
      "⚠️ 这两条不是可选的风格检查,CI 上必须有 Python3。")
endif()
